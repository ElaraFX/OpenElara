#include "mainwindow.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QFileInfo>
#include <QSettings>
#include <QTranslator>
#include <QSharedMemory>

#include<QtCore/QtPlugin>
Q_IMPORT_PLUGIN (QWindowsIntegrationPlugin);
  
#pragma comment (lib,"opengl32.lib")  
  
#ifdef _DEBUG   //Debug mode  
#pragma comment (lib,"Qt5PlatformSupportd.lib")  
#pragma comment (lib,"qwindowsd.lib")
#else           //Release mode 
#pragma comment (lib,"Qt5WinExtras.lib")  
#pragma comment (lib,"Qt5PlatformSupport.lib")  
#pragma comment (lib,"qwindows.lib")
#endif  

int main(int argc, char *argv[])
{
    QSharedMemory singletonCheck;
    singletonCheck.setKey("erGUI_Check");
    if (!singletonCheck.create(1)) return 1;

    QApplication app(argc, argv);
    int lang = 0;
    QSettings setting(QApplication::applicationDirPath() + "/ergui.ini", QSettings::IniFormat);
    if (setting.contains("Tools/Language"))
    {
        lang = setting.value("Tools/Language").toInt();
    }
    QTranslator translator;
    if (lang == 1
        && translator.load(":/translate/ch_cn"))
    {
        app.installTranslator(&translator);
    }
    int layout = 0;
    if (setting.contains("App/Layout"))
    {
        layout = setting.value("App/Layout").toInt();
    }
    app.setApplicationVersion("1.0");
    QCommandLineParser cmdParser;
    cmdParser.addHelpOption();
    cmdParser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);
    cmdParser.setApplicationDescription(QApplication::tr("This is Elara renderer"));
    cmdParser.addPositionalArgument(QApplication::tr("[file1] [file2]"), QApplication::tr("ESS files to render."));

    QCommandLineOption optPreset(QStringList() << "p" << "preset", QCoreApplication::translate("main", "Specify render preset"), "None");
    cmdParser.addOption(optPreset);

    QCommandLineOption optRenderNow(QStringList() << "r" << "render", QCoreApplication::translate("main", "Immeidate render after launched"));
    cmdParser.addOption(optRenderNow);

    QCommandLineOption optPanorama(QStringList() << "n" << "panorama", QCoreApplication::translate("main", "Enable panorama mode by default"));
    cmdParser.addOption(optPanorama);

	QCommandLineOption optCode1(QStringList() << "c1" << "code1", QCoreApplication::translate("main", "Specify code1"), "0");
	cmdParser.addOption(optCode1);

	QCommandLineOption optCode2(QStringList() << "c2" << "code2", QCoreApplication::translate("main", "Specify code2"), "0");
	cmdParser.addOption(optCode2);

	QCommandLineOption optLicense(QStringList() << "l" << "license", QCoreApplication::translate("main", "Specify license"), "0-0-0-0-0-0-0");
	cmdParser.addOption(optLicense);

    cmdParser.process(QCoreApplication::arguments());

    QStringList& fileLists = cmdParser.positionalArguments();

    MainWindow frmMain;
    frmMain.show();
    frmMain.SetLayout(layout);

    bool isProjFile = false;
    if (fileLists.count() == 1)
    {
        QFileInfo testFile(fileLists[0]);
        if (testFile.suffix() == "erp")
        {
            isProjFile = true;
            frmMain.OpenProject(fileLists[0]);
        }
    }
    if (!isProjFile)
    {
        frmMain.AddESSFiles(fileLists, true);
    }

    if (cmdParser.isSet(optPreset))
    {
        frmMain.SetPreset(cmdParser.value(optPreset));
    }

    if (cmdParser.isSet(optPanorama))
    {
        frmMain.EnablePanorama(true);
    }

	if (cmdParser.isSet(optCode1))
	{
		frmMain.SetCode1(cmdParser.value(optCode1));
	}

	if (cmdParser.isSet(optCode2))
	{
		frmMain.SetCode2(cmdParser.value(optCode2));
	}

	if (cmdParser.isSet(optLicense))
	{
		frmMain.SetLicense(cmdParser.value(optLicense));
	}

    if (cmdParser.isSet(optRenderNow))
    {
        frmMain.RenderTasks();
    }

    int result = app.exec();
    setting.setValue("App/Layout", frmMain.LastLayout());
    singletonCheck.detach();
    return result;
}
