QT += core
QT -= gui

INCLUDEPATH += ../Elara_SDK_1_0_65/include

LIBS += ../Elara_SDK_1_0_65/lib/liber.lib \
        shell32.lib

CONFIG += c++11

TARGET = erconsole
CONFIG += console
CONFIG -= app_bundle

TEMPLATE = app

SOURCES += \
    main.cpp
