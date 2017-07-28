#include "optiondialog.h"
#include "ui_optiondialog.h"
#include <QFileDialog>
#include <QSettings>
#include <QTextStream>
#include <QDebug>
#include <QMessageBox>

OptionDialog::OptionDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::OptionDialog),
    mLanguage(0)
{
    ui->setupUi(this);
    QSettings setting(QApplication::applicationDirPath() + "/ergui.ini", QSettings::IniFormat);
    qDebug() << QApplication::applicationDirPath() + "/ergui.ini";
    if (setting.contains("Tools/ShaderPath"))
    {
        ui->txtShaderPath->setPlainText(setting.value("Tools/ShaderPath").toString());
    }
    if (setting.contains("Tools/TexturePath"))
    {
        ui->txtTexturePath->setPlainText(setting.value("Tools/TexturePath").toString());
    }

    mLanguage = 0;
    if (setting.contains("Tools/Language"))
    {
        mLanguage = setting.value("Tools/Language").toInt();
        if (mLanguage < 0 || mLanguage >= ui->cmbLanguage->count())
        {
            mLanguage = 0;
        }
    }
    ui->cmbLanguage->setCurrentIndex(mLanguage);
}

OptionDialog::~OptionDialog()
{
    delete ui;
}

void OptionDialog::on_btnShaderPath_clicked()
{
    QString path = QFileDialog::getExistingDirectory(
                this,
                tr("Shader Directory"),
                ".",
                QFileDialog::ShowDirsOnly
                | QFileDialog::DontResolveSymlinks);
    if (!path.isEmpty())
    {
        ui->txtShaderPath->setText(path);
    }
}

void OptionDialog::on_buttonBox_accepted()
{
    QString shaderPath = ui->txtShaderPath->document()->toPlainText();
    QString texturePath = ui->txtTexturePath->document()->toPlainText();
    QSettings setting(QApplication::applicationDirPath() + "/ergui.ini", QSettings::IniFormat);
    setting.setValue("Tools/ShaderPath", shaderPath);
    setting.setValue("Tools/TexturePath", texturePath);
    setting.setValue("Tools/Language", ui->cmbLanguage->currentIndex());
    QDir info(shaderPath);
    if (info.exists())
    {
        QFile mgr(QApplication::applicationDirPath() + "/manager.ini");
        if (mgr.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            QTextStream txtOutput(&mgr);
            txtOutput << "searchpath " << shaderPath;
            mgr.close();;
        }
    }
    if (ui->cmbLanguage->currentIndex() != mLanguage)
    {
        QMessageBox::information(this, tr("Message"), tr("Please restart GUI to apply language"));
    }
    close();
}

void OptionDialog::on_buttonBox_rejected()
{
    close();
}

void OptionDialog::on_btnTexPath_clicked()
{
    QString path = QFileDialog::getExistingDirectory(
                this,
                tr("Texture Directory"),
                ".",
                QFileDialog::ShowDirsOnly
                | QFileDialog::DontResolveSymlinks);
    if (!path.isEmpty())
    {
        ui->txtTexturePath->setText(path);
    }
}
