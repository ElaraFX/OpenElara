***
Download Qt 5.6.1 open source version from  
http://download.qt.io/official_releases/qt/5.6/5.6.1/single/qt-everywhere-opensource-src-5.6.1.zip
  
Build:  
Under VS command line mode (For example, if you need vs2010 + 64bit, use 64bit command line), enter the extracted directory. Run following commands:  
configure -confirm-license -opensource -platform win32-msvc2010 -debug-and-release -prefix "e:\qt\qt-static(your path)" -no-qml-debug -nomake tests -nomake examples -mp -opengl desktop  
when configure finish, enter:  
nmake  
And then when nmake finish, enter:  
nmake install  
the qt files and libs will be installed to path you set in configuration.

***
Download Elara SDK from  
http://www.rendease.com/#download
  
1. Before build, extract elera sdk to xhome folder.  
The folder should like this:  
xhome  
  \  
    Elara_SDK  
   |  
    buildinc  
   |  
    erconsole  
   |  
    ergui  
  
2. Open buildinc.pro, erconsole.pro and ergui.pro using Qt Creator.  
Build order: buildinc, erconsole, ergui.
After "buildinc" is built, put the generated buildinc.exe under ergui's source code directory.
After built erconsole and ergui, there should be a bin\debug(release) folder under xhome.  
Copy Qt's \msvc2010_64\plugins\platforms to xhome\bin\debug(release)  
Copy *.preset to xhome\bin\debug(release)  
Copy all files except er.exe in Elara SDK's bin folder to xhome\bin\debug(release)  
Copy following qt's dll: Qt5Core.dll Qt5Gui.dll Qt5Widgets.dll to xhome\bin\debug(release)  
[Optional] Copy shaders to xhome\bin\debug(release) and copy elara sdk shaders to replace shaders folder in debug(release).Remove shader files except oso file.
  
### Troubleshooting
  
- If the program can't start due to can't find qt windows plugin, copy \msvc2010_64\plugins\platforms directory to the ergui.exe's directory.

- If build erconsole fails due to "can't find ei.h" or "can't find liber.lib"  
1. Make sure Elara SDK is on top of erconsole  
2. Check erconsole.pro, it should contain following settings:  
INCLUDEPATH += ../Elara_SDK/include  
LIBS += ../Elara_SDK/lib/liber.lib  
Make sure the paths exist and relative to erconsole.pro  
If still build fails, do clean and completely rebuild the project.  