#-------------------------------------------------
#
# Project created by QtCreator 2016-03-30T11:59:26
#
#-------------------------------------------------

QT       += core gui widgets

TARGET = ergui
TEMPLATE = app

INCLUDEPATH += ..\Elara_SDK_1_0_65\include
LIBS += ..\Elara_SDK_1_0_65\lib\liber.lib

TRANSLATIONS+=cn.ts

SOURCES += main.cpp\
        mainwindow.cpp \
    qcustomlabel.cpp \
    optiondialog.cpp

HEADERS  += mainwindow.h \
    qcustomlabel.h \
    optiondialog.h

FORMS    += mainwindow.ui \
    optiondialog.ui

RESOURCES += \
    resource.qrc

RC_FILE = ergui.rc
