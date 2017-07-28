#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "optiondialog.h"
#include "ergui_buildnum.h"
#include <QFileDialog>
#include <QDebug>
#include <QDrag>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QImageReader>
#include <QImageWriter>
#include <QMessageBox>
#include <QProcess>
#include <QFileInfo>
#include <QClipboard>
#include <QSettings>
#include <QAction>
#include <QDesktopServices>
#include <QStyleFactory>
#include <QUrl>
#include <QtXml/QtXml>

#define W_ICONSIZE 236
#define H_ICONSIZE 130

inline QString GetResultPath(const QString& sceneFile)
{
    return sceneFile.left(sceneFile.lastIndexOf('.'));
}

QIcon CreateThumb(const QImage* image)
{
    if (nullptr == image) return QIcon();
    if ((float)image->width() / (float)image->height() < (196.0f / 108.0f))
    {
        float height = (float)image->width() * H_ICONSIZE / W_ICONSIZE;
        int y = (image->height() - height) / 2;
        QImage tmpthumb = image->copy(0, y, image->width(), height);
        return QIcon(QPixmap::fromImage(tmpthumb.scaledToWidth(W_ICONSIZE)));
    }
    else
    {
        float width = (float)image->height() * W_ICONSIZE / H_ICONSIZE;
        int x = (image->width() - width) / 2;
        QImage tmpthumb = image->copy(x, 0, width, image->height());
        return QIcon(QPixmap::fromImage(tmpthumb.scaledToHeight(H_ICONSIZE)));
    }
}

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    mRenderProcess(0),
    mProjectName("Untitled"),
    mbProjectDirty(false),
    mLastLayout(0)
{
    ui->setupUi(this);
    mSharedMemTimer.setInterval(50);

    connect(&mRenderProcess,
            static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this,
            &MainWindow::RenderThreadFinished);

    connect(&mRenderProcess,
            static_cast<void(QProcess::*)()>(&QProcess::readyRead),
            this,
            &MainWindow::ReadFromClient);

    connect(&mSharedMemTimer,
            &QTimer::timeout,
            this,
            &MainWindow::onSharedMemTimer);

    ui->lstFiles->setIconSize(QSize(W_ICONSIZE, H_ICONSIZE));
    ui->lstFiles->setViewMode(QListView::IconMode);

    QAction *actionDeleteFile = new QAction("delete", this);
    connect(actionDeleteFile,
            &QAction::triggered,
            this,
            &MainWindow::onActionDeleteFile_triggered);
    ui->lstFiles->addAction(actionDeleteFile);
    ui->tvPreset->setColumnWidth(0, 190);
    ui->tvPreset->setColumnWidth(1, 70);

    tabifyDockWidget(ui->dockFileView, ui->dockPreset);
    ui->dockFileView->raise();
    ui->statusBar->addWidget(&mScaleText);
    ui->statusBar->addWidget(&mScaleText);
    ui->statusBar->addWidget(&mStatusText);
    ui->statusBar->addWidget(&mpgsRender);
    mStatusText.setText(tr("    Ready    "));
    mScaleText.setText(tr("    Image Scale: ") + "100%  ");
    mpgsRender.setMaximum(10000);
    mpgsRender.setVisible(false);

    mErConsolePath = "\"" + QApplication::applicationDirPath() + "/erconsole.exe" + "\"";
    ui->imageViewer->SetToneEnabled(ui->grpExposureCtrl->isChecked());
    QSettings setting(QApplication::applicationDirPath() + "/ergui.ini", QSettings::IniFormat);
    if (setting.contains("Tools/TexturePath"))
    {
        mTexturePath = setting.value("Tools/TexturePath").toString();
    }
    mCommandMem.setKey("erGUI_signal");
    InitializePresets();
    ApplyToneMapper();
    QApplication::setStyle(QStyleFactory::create("Fusion"));
}

MainWindow::~MainWindow()
{
    mRenderProcess.kill();
    mRenderProcess.waitForFinished();
    delete ui;
}

void MainWindow::RenderTasks()
{
    for (int i = 0; i < ui->lstFiles->count(); ++i)
    {
        QListWidgetItem* pItem = ui->lstFiles->item(i);
        QFileInfo info(pItem->data(Qt::UserRole).toString());
        if (info.exists())
        {
            mQueue.push(info.absoluteFilePath());
            mFilesInQueue.insert(info.absoluteFilePath());
        }
    }
    ui->btnRender->setEnabled(mFilesInQueue.size() > 0);
    RenderNext();
}

void MainWindow::SetPreset(QString &presetName)
{
    if (presetName.isEmpty()) return;
    int idx = ui->cmbPreset->findText(presetName, Qt::MatchStartsWith);
    if (idx != -1)
    {
        ui->cmbPreset->setCurrentIndex(idx);
        ui->smpPreset->setCurrentIndex(idx);
    }
}

void MainWindow::SetCode1(QString &c)
{
	mCode1 = c;
}

void MainWindow::SetCode2(QString &c)
{
	mCode2 = c;
}

void MainWindow::SetLicense(QString &l)
{
    mLicense = l;
}

void MainWindow::EnablePanorama(bool value)
{
    ui->chkPanorama->setChecked(value);
}

void MainWindow::SafeClose()
{
    //if (NeedCancelAction()) return;
    while(!mQueue.empty()) mQueue.pop();
    mFilesInQueue.clear();
    close();
}

void MainWindow::AddESSFiles(QStringList &list, bool dontDirty)
{
    QStringList::Iterator it = list.begin();
    for (; it != list.end(); ++it)
    {
        QFileInfo info(*it);
        if (info.exists())
        {
            AddFileItem(*it);
        }
    }
    if (!dontDirty)
    {
        mbProjectDirty = list.count() > 0;
    }
}

void MainWindow::UpdateImageStatus(QListWidgetItem* item, bool resetView)
{
    if (nullptr == item)
    {
        ui->imageViewer->Reset();
        return;
    }
    QString& sceneFile = item->data(Qt::UserRole).toString();
    if (mFilesInQueue.find(sceneFile) != mFilesInQueue.end()
            || mCurrentScene == sceneFile)
    {
        ui->btnRender->setText(tr("Cancel(Esc)"));
        ui->smpRender->setText(tr("Cancel(Esc)"));
    }
    else
    {
        ui->btnRender->setText(tr("Render"));
        ui->smpRender->setText(tr("Render"));
    }

    if (!mCurrentScene.isEmpty()) return;

    QString resultPath = GetResultPath(sceneFile) + "/temp.erc";
    QFileInfo info(resultPath);
    if (!info.exists())
    {
        ui->imageViewer->Reset();
        return;
    }

    ui->imageViewer->LoadFromCache(resultPath);
    if (resetView) ui->imageViewer->FitImage();
    ui->imageViewer->Refresh();
    item->setIcon(CreateThumb(ui->imageViewer->image()));

    QString title(tr("Elara Renderer"));
    title += " - ";
    title += sceneFile;
    this->setWindowTitle(title);
}

void MainWindow::FileItemChanged(QListWidgetItem * item)
{
    UpdateImageStatus(item, true);
}

void MainWindow::FitImage()
{
    ui->imageViewer->FitImage();
}

void MainWindow::FitControl()
{
    ui->imageViewer->FitControl();
}

void MainWindow::ShowOption()
{
    OptionDialog optDlg(this);
    optDlg.exec();
    QSettings setting(QApplication::applicationDirPath() + "/ergui.ini", QSettings::IniFormat);
    if (setting.contains("Tools/TexturePath"))
    {
        mTexturePath = setting.value("Tools/TexturePath").toString();
    }
}

void UpdateThumbnail(QListWidget* lstFiles, QString& key, QCustomLabel* imgViewer, bool saveCache)
{
    for (int i = 0; i < lstFiles->count(); ++i)
    {
        auto item = lstFiles->item(i);
        QString itemScene = item->data(Qt::UserRole).toString();
        if (itemScene != key) continue;
        item->setIcon(CreateThumb(imgViewer->image()));
        if (saveCache)
        {
            QString sceneResultPath = GetResultPath(key);
                imgViewer->SaveToCache(sceneResultPath + "/temp.erc");
                imgViewer->Refresh();
            }
        }
}

void MainWindow::RenderThreadFinished(int , QProcess::ExitStatus)
{
    mSharedMemTimer.stop();
    UpdateThumbnail(ui->lstFiles, mCurrentScene, ui->imageViewer, true);
    if (ui->lstFiles->currentItem()->data(Qt::UserRole).toString() == mCurrentScene)
    {
        ui->btnRender->setText(tr("Render"));
        ui->smpRender->setText(tr("Render"));
    }
    mpgsRender.setValue(mpgsRender.maximum());
    mpgsRender.setVisible(false);
    mCurrentScene = "";
    int espTime = mRenderTime.elapsed() / 1000;
    QString outState = tr("    Last render time: ");
    outState += QString::number(espTime / 3600) + ":";
    espTime %= 3600;
    outState += QString::number(espTime / 60) + ":";
    espTime %= 60;
    outState += QString::number(espTime);
    outState += "   ";
    mStatusText.setText(outState);
    UpdateImageStatus(ui->lstFiles->currentItem(), false);
    RenderNext();
}

void MainWindow::ReadFromClient()
{
    QString consoleOut(mRenderProcess.readAllStandardOutput());
    if (consoleOut.isEmpty()) return;
    ui->txtConsole->setPlainText(ui->txtConsole->toPlainText() + consoleOut);
    ui->txtConsole->moveCursor(QTextCursor::End);
}

void MainWindow::onImageScaleChanged(float value)
{
    mScaleText.setText(tr("Image Scale: ") + QString::number((int)(value * 100)) + "%  ");
}

void MainWindow::onSharedMemTimer()
{
    mSharedMem.setKey("erConsole_data");
    if (!mSharedMem.attach()) return;
    mSharedMem.lock();
    unsigned char* rawData = (unsigned char*)mSharedMem.data();
    size_t width = *(size_t*)rawData;
    rawData += sizeof(size_t);
    size_t height = *(size_t*)rawData;
    rawData += sizeof(size_t);
    size_t percent = *(size_t*)rawData;
    mpgsRender.setValue((int)percent);
    rawData += sizeof(size_t);
    if (0 == width || 0 == height)
    {
        mSharedMem.unlock();
        mSharedMem.detach();
        return;
    }
    ui->imageViewer->SetRawData((int)width, (int)height, (float*)rawData);
    mSharedMem.unlock();
    mSharedMem.detach();
    ui->imageViewer->Refresh();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasText())
        e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *e)
{
    if (e->mimeData()->hasText() && !e->mimeData()->text().isEmpty())
    {
        QString& path = e->mimeData()->text();
        QString fn = path.right(path.length() - path.lastIndexOf('/') - 1);
        QListWidgetItem* fileItem = new QListWidgetItem(QIcon(path), fn);
        fileItem->setData(Qt::UserRole, path);
        ui->lstFiles->addItem(fileItem);
    }
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    NeedCancelAction() ? e->ignore() : e->accept();
}

void MainWindow::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape)
    {
        on_btnRender_clicked();
    }
}

void MainWindow::RenderNext()
{
    if (mCurrentScene != "") return;
    if (mQueue.empty()) return;
    QString nextFile = mQueue.front();
    mQueue.pop();
    mFilesInQueue.erase(nextFile);
    QString sceneResultPath = GetResultPath(nextFile);

    QDir dirMaker;
    if (!dirMaker.exists(sceneResultPath))
    {
        dirMaker.mkpath(sceneResultPath);
    }
    else if (QFileInfo::exists(sceneResultPath + "/temp.erc"))
    {
        QFile::remove(sceneResultPath + "/temp.erc");
    }
    mCurrentScene = nextFile;
    QString cmd = mErConsolePath;
    cmd += " \"" + nextFile + "\"";
    cmd += " -sharemem";

	if (!mCode1.isEmpty() && !mCode2.isEmpty() && !mLicense.isEmpty())
	{
		cmd += " -license";
		cmd += " " + mCode1;
		cmd += " " + mCode2;
		cmd += " " + mLicense;
	}	

    cmd += " -output color color ";
    cmd += ui->chkEnableFilter->isChecked() ? "on " : "off ";
    cmd += ui->chkEnableGamma->isChecked() ? "on " : "off ";
    cmd += "off "; //Exposure control is post effect. Ignore here.
    cmd += "temp.png";
    cmd += PresetToString();

    if (!mTexturePath.isEmpty())
    {
        cmd += " -texture_searchpath \"" + mTexturePath + "\"";
    }

    if (ui->lstFiles->currentItem()->data(Qt::UserRole).toString() == nextFile)
    {
        ui->btnRender->setText(tr("Cancel(Esc)"));
        ui->smpRender->setText(tr("Cancel(Esc)"));
    }
    ui->txtConsole->setText("");
    ui->imageViewer->Reset();
    mRenderProcess.setProcessChannelMode(QProcess::MergedChannels);
    mRenderProcess.start(cmd, QIODevice::ReadOnly | QIODevice::Unbuffered);
    qDebug() << cmd;
    mSharedMemTimer.start();
    mpgsRender.setValue(0);
    mpgsRender.setVisible(true);
    mStatusText.setText(tr("    Rendering...   "));
    mRenderTime.start();
}

void MainWindow::AddFileItem(QString &filename)
{
    QFileInfo fileInfo(filename);
    if (!fileInfo.exists()) return;
    QString fn = fileInfo.fileName();
    QPixmap defaultIcon(":/images/default_logo");
    QListWidgetItem* fileItem = new QListWidgetItem(QIcon(defaultIcon.scaled(QSize(W_ICONSIZE,H_ICONSIZE))), fn);
    fileItem->setData(Qt::UserRole, fileInfo.absoluteFilePath());
    ui->lstFiles->addItem(fileItem);
    ui->lstFiles->clearSelection();
    ui->lstFiles->setCurrentItem(fileItem, QItemSelectionModel::Select);
    fileItem->setToolTip(filename);
    FileItemChanged(fileItem);
    ui->btnRender->setEnabled(true);
}

void MainWindow::on_btnRender_clicked()
{
    if (ui->lstFiles->currentItem() == nullptr) return;
    QString fileToRender = ui->lstFiles->currentItem()->data(Qt::UserRole).toString();
    if (!CancelJob())//Launch new job
    {
        mQueue.push(fileToRender);
        mFilesInQueue.insert(fileToRender);
        ui->btnRender->setText(tr("Cancel(Esc)"));
        ui->smpRender->setText(tr("Cancel(Esc)"));
    }
    RenderNext();
}

bool MainWindow::CancelJob()
{
    if (nullptr == ui->lstFiles->currentItem()) return false;

    QString fileToRender = ui->lstFiles->currentItem()->data(Qt::UserRole).toString();
    if (mCurrentScene == fileToRender) //Cancel current job
    {
        if (mCommandMem.attach())
        {
            mCommandMem.lock();
            GUICommand* guiCmd = (GUICommand*)mCommandMem.data();
            guiCmd->cmd = 1;
            mCommandMem.unlock();
            mCommandMem.detach();
        }
        else
        {
            mRenderProcess.kill();
        }

        mRenderProcess.waitForFinished();
        ui->btnRender->setText(tr("Render"));
        ui->smpRender->setText(tr("Render"));
        return true;
    }
    else if (mFilesInQueue.find(fileToRender) != mFilesInQueue.end()) //Already in queue, we need to cancel it.
    {
        RenderQueue newQueue;
        while(!mQueue.empty())
        {
            QString it = mQueue.front();
            mQueue.pop();
            if (it != fileToRender)
            {
                newQueue.push(it);
            }
        }
        mQueue.swap(newQueue);
        mFilesInQueue.erase(fileToRender);
        ui->btnRender->setText(tr("Render"));
        ui->smpRender->setText(tr("Render"));
        return true;
    }
    return false;
}

void MainWindow::InitializePresets()
{
    QDir pstDir(QApplication::applicationDirPath());
    QStringList pstFilter;
    pstFilter << "*.preset";
    QFileInfoList fiList = pstDir.entryInfoList(pstFilter);
    ui->cmbPreset->addItem(tr("None"));
    ui->smpPreset->addItem(tr("None"));
    QFileInfoList::Iterator it = fiList.begin();
    for (; it != fiList.end(); ++it)
    {
        ui->cmbPreset->addItem(it->baseName());
        ui->smpPreset->addItem(it->baseName());
    }
}

QString MainWindow::PresetToString()
{
    QTreeWidgetItem* pCurItem = ui->tvPreset->currentItem();
    if (pCurItem)
    {
        on_tvPreset_currentItemChanged(nullptr, pCurItem);
    }
    QString outString;
    bool usePresetCam = ui->actionSimple_Style->isChecked();
    bool progressiveAdded = false;
    for (int i = 0; i < ui->tvPreset->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* pGroupItem = ui->tvPreset->topLevelItem(i);
        if (pGroupItem->text(0) == "camera"
            && usePresetCam)
        {
            continue;
        }
        outString += " -" + pGroupItem->text(0) + " \"";
        for (int childIdx = 0; childIdx < pGroupItem->childCount(); ++childIdx)
        {
            QTreeWidgetItem* pChildItem = pGroupItem->child(childIdx);
            outString += pChildItem->text(0) + "=" + pChildItem->text(1) +";";
        }
        if (!progressiveAdded && pGroupItem->text(0) == "options")
        {
            outString += "progressive=";
            outString += ui->smpProgressive->isChecked() ? "on;" : "off;";
            progressiveAdded = true;
        }
        outString += "\"";
    }
    bool usePano = false;
    if (usePresetCam)
    {
        QString preStr = ui->smpResolution->currentText();
        int x = -1;
        int y = -1;
        if (preStr.contains(':'))
        {
            usePano = true;
            preStr = preStr.split(": ")[1];
            y = preStr.toInt();
            x = y * 6;
        }
        else
        {
            QStringList strRes = preStr.split(" x ");
            x = strRes[0].toInt();
            y = strRes[1].toInt();
        }

        outString += " -camera \"res_x=" + QString::number(x) + ";res_y=" + QString::number(y) + ";\"";
    }
    else if (ui->chkPanorama->isChecked())
    {
        usePano = true;
    }
    if (usePano)
    {
        outString += " -lens cubemap_camera off 0.0";
    }
    return outString;
}

void MainWindow::onActionDeleteFile_triggered()
{
    if (nullptr == ui->lstFiles->currentItem()) return;
    CancelJob();
    int curRow = ui->lstFiles->currentRow();
    QListWidgetItem* curItem = ui->lstFiles->takeItem(curRow);
    delete curItem;
    if (ui->lstFiles->count() == 0)
    {
        ui->btnRender->setEnabled(false);
    }
    FileItemChanged(ui->lstFiles->currentItem());
    mbProjectDirty = true;
}

bool MainWindow::NeedCancelAction()
{
    if (mbProjectDirty)
    {
        int question = QMessageBox::question(this,
                                             tr("Confirm Save"),
                                             tr("Do you want to save the project?"),
                                             QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (question == QMessageBox::Yes)
        {
            on_action_Save_triggered();
        }
        else if (question == QMessageBox::Cancel)
        {
            return true;
        }
    }
    return false;
}

void MainWindow::on_action_New_triggered()
{
    if (NeedCancelAction()) return;
    mProjectName = tr("Untitled");
    CancelJob();
    FileItemChanged(nullptr);
    ui->lstFiles->clear();
}

void MainWindow::on_action_Document_triggered()
{
    QDesktopServices::openUrl(QUrl("https://github.com/ElaraFX/elaradoc/wiki"));
}

void MainWindow::on_action_Contact_Us_triggered()
{
    QDesktopServices::openUrl(QUrl("mailto:marketing@rendease.com"));
}

void MainWindow::on_action_Homepage_triggered()
{
    QDesktopServices::openUrl(QUrl("http://rendease.com/"));
}

void MainWindow::PresetChanged(const QString& arg1)
{
    ui->tvPreset->clear();
    QString pstFile = QApplication::applicationDirPath() + "/" + arg1;
    if (!QFile::exists(pstFile))
    {
        pstFile += ".preset";
        if (!QFile::exists(pstFile))
        {
            return;
        }
    }

    QSettings setting(pstFile, QSettings::IniFormat);
    QStringList groups = setting.childGroups();
    for (QStringList::Iterator it = groups.begin();
         it != groups.end();
         ++it)
    {
        QTreeWidgetItem* pGroupItem = new QTreeWidgetItem(QStringList() << *it);
        ui->tvPreset->addTopLevelItem(pGroupItem);
        setting.beginGroup(*it);
        QStringList keys = setting.allKeys();
        for (QStringList::Iterator keyIt = keys.begin();
             keyIt != keys.end();
             ++keyIt)
        {
            QString val = setting.value(*keyIt).toString();
            QTreeWidgetItem* paramItem = new QTreeWidgetItem(QStringList() << *keyIt << val);
            paramItem->setFlags(paramItem->flags() | Qt::ItemIsEditable);
            pGroupItem->addChild(paramItem);
        }
        setting.endGroup();
    }
    ui->tvPreset->expandAll();
}

void MainWindow::on_cmbPreset_currentIndexChanged(const QString &arg1)
{
    PresetChanged(arg1);
}

void MainWindow::on_btnSavePreset_pressed()
{
    QFileDialog dialog(this, tr("Save Preset As"));
    dialog.setNameFilter(tr("Elara Preset (*.preset)"));
    dialog.setDefaultSuffix("preset");
    QString presetName = ui->cmbPreset->currentText();
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.selectFile(presetName);
    dialog.exec();
    if (dialog.selectedFiles().count() == 0)
    {
        return;
    }
    QFileInfo info(dialog.selectedFiles().first());
    QSettings* presetWriter = new QSettings(info.absoluteFilePath(), QSettings::IniFormat);
    presetWriter->clear();
    for (int i = 0; i < ui->tvPreset->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* pGroupItem = ui->tvPreset->topLevelItem(i);
        presetWriter->beginGroup(pGroupItem->text(0));
        for (int childIdx = 0; childIdx < pGroupItem->childCount(); ++childIdx)
        {
            QTreeWidgetItem* pChildItem = pGroupItem->child(childIdx);
            presetWriter->setValue(pChildItem->text(0), pChildItem->text(1));
        }
        presetWriter->endGroup();
    }
    delete presetWriter; //Delete QSettings to force writing to disk
    presetWriter = nullptr;

    if (ui->cmbPreset->findText(info.baseName(), Qt::MatchStartsWith) == -1)
    {
        ui->cmbPreset->addItem(info.baseName());
        ui->cmbPreset->setCurrentIndex(ui->cmbPreset->count() - 1);
    }
}

void MainWindow::on_action_Copy_triggered()
{
    if (mCurrentScene != "") return;
    if (ui->imageViewer->image() == nullptr) return;
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setImage(*(ui->imageViewer->image()));
}

void MainWindow::on_actionAdd_Ess_File_triggered()
{
    QStringList essFiles = QFileDialog::getOpenFileNames(
                this,
                tr("Open ESS File"),
                ".",
                tr("Elara Scene Script File (*.ess)"));

    AddESSFiles(essFiles, false);
}

void MainWindow::on_action_Save_triggered()
{
    if (!mbProjectDirty) return;
    if (mProjectName == "Untitled")
    {
        mProjectName = QFileDialog::getSaveFileName(
                    this,
                    tr("Save Project File"),
                    ".",
                    tr("Elara Project File (*.erp)"));
    }
    if (mProjectName.isEmpty())
    {
        mProjectName = "Untitled";
        return;
    }

    QFile projFile(mProjectName);
    if (!projFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, tr("Warning"), tr("Failed to save project!"));
        return;
    }
    for (int i = 0; i < ui->lstFiles->count(); ++i)
    {
        QListWidgetItem* pItem = ui->lstFiles->item(i);
        QString essFile = pItem->data(Qt::UserRole).toString() + "\r\n";
        projFile.write(essFile.toStdString().data());
    }
    projFile.close();
    mbProjectDirty = false;
}

void MainWindow::on_action_SaveImage_triggered()
{
    if (!ui->lstFiles->currentItem()
        || ui->imageViewer->image() == nullptr)
    {
        return;
    }
    const QImage* img = ui->imageViewer->image();
    bool splitImage = false;
    if (img->width() == img->height() * 6)
    {
        int question = QMessageBox::question(this,
                                             tr("Split Image"),
                                             tr("Do you want save cubemap faces as 6 files?"),
                                             QMessageBox::Yes | QMessageBox::No);
        splitImage = question == QMessageBox::Yes;
    }
    QFileDialog dialog(this, tr("Save Image As"));
    QStringList mimeTypeFilters;
    const QByteArrayList supportedMimeTypes = QImageWriter::supportedMimeTypes();
    foreach (const QByteArray &mimeTypeName, supportedMimeTypes)
    {
        mimeTypeFilters.append(mimeTypeName);
    }
    mimeTypeFilters.sort();
    dialog.setMimeTypeFilters(mimeTypeFilters);
    dialog.selectMimeTypeFilter("image/png");
    dialog.setDefaultSuffix("png");
    QString origName = ui->lstFiles->currentItem()->data(Qt::UserRole).toString();
    if (origName.lastIndexOf('.') > 0)
    {
        origName = origName.left(origName.lastIndexOf('.'));
    }
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.selectFile(origName);
    dialog.exec();
    if (dialog.result() == QDialog::Rejected)
    {
        return;
    }

    static const QString cubeExt[] = {"_LF", "_RT", "_UP_WEB", "_DN_WEB", "_FR", "_BK"};
    const QString fn = dialog.selectedFiles().first();
    if (splitImage)
    {
        QFileInfo fi(fn);
        int counter = 0;
        for (int x = 0; x < img->width(); x += img->height())
        {
            QImage temp = img->copy(x, 0, img->height(), img->height());
            QString filename = fi.absolutePath() + fi.baseName() + cubeExt[counter] + "." + fi.suffix();
            temp.save(filename);
            ++counter;
        }
    }
    else if (!ui->imageViewer->SaveImage(fn))
    {
        QMessageBox::information(this, QGuiApplication::applicationDisplayName(),
                                 tr("Cannot write %1").arg(QDir::toNativeSeparators(fn)));
    }
}

void MainWindow::OpenProject(QString &projFilename)
{
    if (!QFile::exists(projFilename)) return;
    mProjectName = projFilename;

    QFile projFile(mProjectName);
    if (!projFile.exists()) return;

    if (!projFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, tr("Open Failed"), tr("Failed to open the project!"));
        return;
    }
    QStringList essFiles;
    QTextStream stream(&projFile);
    while(!stream.atEnd())
    {
        essFiles << stream.readLine();
    }
    projFile.close();
    AddESSFiles(essFiles, false);
    mbProjectDirty = false;
}

void MainWindow::on_action_Open_triggered()
{
    if (NeedCancelAction()) return;
    QString projFile = QFileDialog::getOpenFileName(this,
                                                    tr("Open Project File"),
                                                    ".",
                                                    tr("Elara Project File (*.erp)"));
    if (!projFile.isEmpty())
    {
        mbProjectDirty = false;
        on_action_New_triggered();
        OpenProject(projFile);
    }
}

void MainWindow::on_action_About_triggered()
{
    QMessageBox::information(this, tr("About"), tr("Elara GUI version ") + "1.0 Build " + (__TIMESTAMP__));
}

void MainWindow::ApplyToneMapper()
{
    float highlight = (float)ui->sldHighLight->value() / 100.0f;
    float midtones = (float)ui->sldMidTones->value() / 10.0f;
    float shadows = (float)ui->sldShadows->value() / 100.0f;
    float expValues = (float)ui->sldExpValue->value() / 100.0f;
    float colorSat = (float)ui->sldColorSat->value() / 100.0f;
    int whitePt = ui->sldWhitePt->value();
    ui->txtHighLight->setText(QString::number(highlight));
    ui->txtMidTones->setText(QString::number(midtones));
    ui->txtShadows->setText(QString::number(shadows));
    ui->txtExpValue->setText(QString::number(expValues));
    ui->txtColorSat->setText(QString::number(colorSat));
    ui->txtWhitePt->setText(QString::number(whitePt));
    ui->smpTxtExposure->setText(ui->txtExpValue->text());
    if (ui->imageViewer->IsToneEnabled())
    {
        ui->imageViewer->SetToneParameters(expValues, highlight, midtones, shadows, colorSat, whitePt);
    }
    ui->imageViewer->Refresh();
    if (!mCurrentScene.isEmpty())
    {
        UpdateThumbnail(ui->lstFiles, mCurrentScene, ui->imageViewer, false);
    }
    else if (ui->lstFiles->currentItem() != nullptr)
    {
        UpdateThumbnail(ui->lstFiles, ui->lstFiles->currentItem()->data(Qt::UserRole).toString()
                        , ui->imageViewer, false);
    }
}

void SyncSlider(QSlider* slider, QLineEdit* edit, float multiplier)
{
    int value = (int)(edit->text().toFloat() * multiplier);
    int vclamp = value;
    vclamp = value > slider->maximum() ? slider->maximum() : value;
    vclamp = value < slider->minimum() ? slider->minimum() : value;
    if (vclamp != slider->value())
    {
        slider->setValue(value);
    }
    if (vclamp != value)
    {
        edit->setText(QString::number((float)value / multiplier));
    }
}

void MainWindow::on_txtHighLight_editingFinished()
{
    SyncSlider(ui->sldHighLight, ui->txtHighLight, 100.0f);
}

void MainWindow::on_txtMidTones_editingFinished()
{
    SyncSlider(ui->sldMidTones, ui->txtMidTones, 10.0f);
}

void MainWindow::on_txtShadows_editingFinished()
{
    SyncSlider(ui->sldShadows, ui->txtShadows, 100.0f);
}

void MainWindow::on_txtExpValue_editingFinished()
{
    SyncSlider(ui->sldExpValue, ui->txtExpValue, 100.0f);
    SyncSlider(ui->smpExpSlider, ui->txtExpValue, 100.0f);
}

void MainWindow::on_txtColorSat_editingFinished()
{
    SyncSlider(ui->sldColorSat, ui->txtColorSat, 100.0f);
}

void MainWindow::on_txtWhitePt_editingFinished()
{
    SyncSlider(ui->sldWhitePt, ui->txtWhitePt, 1.0f);
}

void MainWindow::on_smpTxtExposure_editingFinished()
{
    SyncSlider(ui->smpExpSlider, ui->smpTxtExposure, 100.0f);
    SyncSlider(ui->sldExpValue, ui->smpTxtExposure, 100.0f);
}


void MainWindow::on_smpExposure_toggled(bool checked)
{
    ui->smpExpSlider->setEnabled(checked);
    ui->grpExposureCtrl->setChecked(checked);
    ui->smpTxtExposure->setEnabled(checked);
}

void MainWindow::on_smpExpSlider_valueChanged(int value)
{
    ui->sldExpValue->setValue(value);
    ApplyToneMapper();
}

void MainWindow::on_grpExposureCtrl_toggled(bool arg1)
{
    ui->imageViewer->SetToneEnabled(arg1);
    ui->smpExposure->setChecked(arg1);
    ui->smpExpSlider->setEnabled(arg1);
    ui->smpTxtExposure->setEnabled(arg1);
    ApplyToneMapper();
}

void MainWindow::on_sldHighLight_valueChanged(int)
{
    ApplyToneMapper();
}

void MainWindow::on_sldMidTones_valueChanged(int)
{
    ApplyToneMapper();
}

void MainWindow::on_sldShadows_valueChanged(int)
{
    ApplyToneMapper();
}

void MainWindow::on_sldExpValue_valueChanged(int value)
{
    ui->smpExpSlider->setValue(value);
    ApplyToneMapper();
}

void MainWindow::on_sldColorSat_valueChanged(int)
{
    ApplyToneMapper();
}

void MainWindow::on_sldWhitePt_valueChanged(int)
{
    ApplyToneMapper();
}

void MainWindow::on_smpRender_clicked()
{
    on_btnRender_clicked();
}

void MainWindow::on_smpPreset_currentIndexChanged(const QString &arg1)
{
    PresetChanged(arg1);
}

void MainWindow::on_actionSimple_Style_triggered()
{
    ui->dockFileView->setVisible(false);
    ui->dockPreset->setVisible(false);
    ui->dockRenderControl->setVisible(false);
    ui->dockConsole->setVisible(false);
    ui->dockSimple->setVisible(true);
    ui->dockSimple->setMaximumHeight(50);
    ui->imageViewer->FitImage();
    ui->actionSimple_Style->setChecked(true);
    ui->actionExpert_Style->setChecked(false);
    mLastLayout = 0;
}

void MainWindow::on_actionExpert_Style_triggered()
{
    ui->dockFileView->setVisible(true);
    ui->dockPreset->setVisible(true);
    ui->dockRenderControl->setVisible(true);
    ui->dockConsole->setVisible(true);
    ui->dockSimple->setVisible(false);
    ui->actionSimple_Style->setChecked(false);
    ui->actionExpert_Style->setChecked(true);
    ui->imageViewer->FitImage();
    mLastLayout = 1;
}

void MainWindow::SetLayout(int layout)
{
    if (layout == 0)
    {
        on_actionSimple_Style_triggered();
    }
    else if (layout == 1)
    {
        on_actionExpert_Style_triggered();
    }
}

void MainWindow::on_tvPreset_itemDoubleClicked(QTreeWidgetItem *item, int)
{
    if (item->text(1).compare("off", Qt::CaseInsensitive) == 0
            || item->text(1).compare("fast", Qt::CaseInsensitive) == 0
			|| item->text(1).compare("accurate", Qt::CaseInsensitive) == 0)
    {
        QComboBox* boolCombo = new QComboBox(ui->tvPreset);
		boolCombo->addItems(QStringList() << "off"
                             << "fast"
                             << "accurate");

		 if (item->text(1).compare("off", Qt::CaseInsensitive) == 0)
        {
            boolCombo->setCurrentIndex(0);
        }
        else if (item->text(1).compare("fast", Qt::CaseInsensitive) == 0)
        {
            boolCombo->setCurrentIndex(1);
        }
        else if (item->text(1).compare("accurate", Qt::CaseInsensitive) == 0)
        {
            boolCombo->setCurrentIndex(2);
        }
        ui->tvPreset->setItemWidget(item, 1, boolCombo);
    }
    if (item->text(0).compare("filter", Qt::CaseInsensitive) == 0)
    {
        QComboBox* filterCombo = new QComboBox(ui->tvPreset);
        filterCombo->addItems(QStringList() << "box"
                             << "triangle"
                             << "catmull-rom"
                             << "gaussian"
                             << "sinc");
        if (item->text(1).compare("box", Qt::CaseInsensitive) == 0)
        {
            filterCombo->setCurrentIndex(0);
        }
        else if (item->text(1).compare("triangle", Qt::CaseInsensitive) == 0)
        {
            filterCombo->setCurrentIndex(1);
        }
        else if (item->text(1).compare("catmull-rom", Qt::CaseInsensitive) == 0)
        {
            filterCombo->setCurrentIndex(2);
        }
        else if (item->text(1).compare("gaussian", Qt::CaseInsensitive) == 0)
        {
            filterCombo->setCurrentIndex(3);
        }
        else if (item->text(1).compare("sinc", Qt::CaseInsensitive) == 0)
        {
            filterCombo->setCurrentIndex(4);
        }

        ui->tvPreset->setItemWidget(item, 1, filterCombo);
    }
}

void MainWindow::on_tvPreset_currentItemChanged(QTreeWidgetItem *, QTreeWidgetItem *previous)
{
    if (previous)
    {
        QWidget* editor = ui->tvPreset->itemWidget(previous, 1);
        QComboBox* combo = dynamic_cast<QComboBox*>(editor);
        if (combo)
        {
            previous->setText(1, combo->currentText());
            ui->tvPreset->setItemWidget(previous, 1, nullptr);
        }
    }
}

void MainWindow::on_btnShare_clicked()
{
    if (ui->imageViewer->image() == nullptr) return;
    const QImage* img = ui->imageViewer->image();
    static const QString cubeExt[] = {"CubeMap11_LF.jpg",
                                      "CubeMap11_RT.jpg",
                                      "CubeMap11_UP_WEB.jpg",
                                      "CubeMap11_DN_WEB.jpg",
                                      "CubeMap11_FR.jpg",
                                      "CubeMap11_BK.jpg"};
    QString basePath = qApp->applicationDirPath() + "/../Panorama/";
    if (!QFileInfo::exists(basePath))
    {
        QDir dir(basePath);
        dir.mkdir(basePath);
        if (!QFileInfo::exists(basePath))
        {
            QMessageBox::warning(this, tr("Warning"), "Failed to create path: " + basePath);
            return;
        }
    }
    QString app_path = QApplication::applicationDirPath();
    if (ui->chkPanorama->isChecked())
    {
        int counter = 0;
        for (int x = 0; x < img->width(); x += img->height())
        {
            QImage temp = img->copy(x, 0, img->height(), img->height());
            QString filename = basePath + cubeExt[counter];
            temp.save(filename);
            ++counter;
        }
		QString cmd =  app_path + "/../pictureupload.exe panorama " + basePath;
        QProcess::startDetached(cmd);
    }
    else
    {
        QString filename = app_path + "/temp.jpg";
        img->save(filename);
        QString cmd = app_path + "/../pictureupload.exe image share " + filename;
        QProcess::startDetached(cmd);
    }
}
