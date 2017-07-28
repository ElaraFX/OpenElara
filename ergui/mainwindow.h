#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QProcess>
#include <QClipboard>
#include <QCloseEvent>
#include <QSharedMemory>
#include <QProgressBar>
#include <QLabel>
#include <QComboBox>
#include <QTime>
#include <QTimer>
#include <queue>
#include <set>
#include <QTreeWidget>

typedef std::queue<QString> RenderQueue;

namespace Ui {
class MainWindow;
}
class QListWidgetItem;
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();
    void AddESSFiles(QStringList& list, bool dontDirty);
    void RenderTasks();
    void SetPreset(QString& presetName);
    void OpenProject(QString& projFilename);
    int LastLayout() {return mLastLayout;}
    void SetLayout(int style);
	void SetCode1(QString &c);
	void SetCode2(QString &c);
	void SetLicense(QString &l);
    void EnablePanorama(bool value);

private slots:
    void SafeClose();
    void FileItemChanged(QListWidgetItem*);
    void FitImage();
    void FitControl();
    void ShowOption();
    void RenderThreadFinished(int, QProcess::ExitStatus);
    void ReadFromClient();
    void onImageScaleChanged(float);

    void onSharedMemTimer();
    void onActionDeleteFile_triggered();
    void on_btnRender_clicked();

    void on_action_Document_triggered();

    void on_action_Contact_Us_triggered();

    void on_action_Homepage_triggered();

    void on_cmbPreset_currentIndexChanged(const QString &arg1);

    void on_btnSavePreset_pressed();

    void on_action_Copy_triggered();

    void on_action_New_triggered();

    void on_action_Open_triggered();

    void on_action_Save_triggered();

    void on_actionAdd_Ess_File_triggered();

    void on_action_SaveImage_triggered();


    void on_action_About_triggered();

    void on_sldHighLight_valueChanged(int);

    void on_sldMidTones_valueChanged(int);

    void on_sldShadows_valueChanged(int);

    void on_sldExpValue_valueChanged(int);

    void on_sldColorSat_valueChanged(int);

    void on_sldWhitePt_valueChanged(int);

    void on_smpRender_clicked();

    void on_smpPreset_currentIndexChanged(const QString &arg1);

    void on_actionSimple_Style_triggered();

    void on_actionExpert_Style_triggered();

    void on_smpExposure_toggled(bool checked);

    void on_smpExpSlider_valueChanged(int value);

    void on_grpExposureCtrl_toggled(bool arg1);

    void on_tvPreset_itemDoubleClicked(QTreeWidgetItem *item, int column);

    void on_tvPreset_currentItemChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous);

    void on_txtHighLight_editingFinished();

    void on_txtMidTones_editingFinished();

    void on_txtShadows_editingFinished();

    void on_txtExpValue_editingFinished();

    void on_txtColorSat_editingFinished();

    void on_txtWhitePt_editingFinished();

    void on_smpTxtExposure_editingFinished();

    void on_btnShare_clicked();

private:
    void UpdateImageStatus(QListWidgetItem* item, bool resetView);
    Ui::MainWindow *ui;

    QProcess mRenderProcess;
    QString mCurrentScene;
    RenderQueue mQueue;
    std::set<QString>  mFilesInQueue;
    QString mErConsolePath;
    QString mTexturePath;
    QTime mRenderTime;
    QLabel mStatusText;
    QLabel mScaleText;
    QProgressBar mpgsRender;

    QTimer mSharedMemTimer;
    QSharedMemory mSharedMem;
    QSharedMemory mCommandMem;

    QString mProjectName;
    bool mbProjectDirty;
    int mLastLayout;

	QString mCode1;
	QString mCode2;
	QString mLicense;

    void ApplyToneMapper();
    bool NeedCancelAction();
    bool CancelJob();
    void RenderNext();
    void InitializePresets();
    QString PresetToString();
    void AddFileItem(QString& filename);
    void dragEnterEvent(QDragEnterEvent *e);
    void dropEvent(QDropEvent *e);
    void closeEvent(QCloseEvent* e) override;
    void keyPressEvent(QKeyEvent *e);
    void PresetChanged(const QString &arg1);
};

#endif // MAINWINDOW_H
