#-------------------------------------------------
#
# Project created by QtCreator 2015-01-19T09:26:40
#
#-------------------------------------------------

QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = vchar64
TEMPLATE = app
VERSION = 0.0.8
GIT_VERSION = $$system(git describe --abbrev=4 --dirty --always --tags)
DEFINES += GIT_VERSION=\\\"$$GIT_VERSION\\\"

CONFIG += c++11
CONFIG += debug_and_release

SOURCES += src/main.cpp\
        src/mainwindow.cpp \
    src/state.cpp \
    src/aboutdialog.cpp \
    src/stateimport.cpp \
    src/stateexport.cpp \
    src/exportdialog.cpp \
    src/tilepropertiesdialog.cpp \
    src/commands.cpp \
    src/palette.cpp \
    src/bigcharwidget.cpp \
    src/charsetwidget.cpp \
    src/colorrectwidget.cpp \
    src/palettewidget.cpp \
    src/xlinkpreview.cpp \
    src/tilesetwidget.cpp \
    src/vchar64application.cpp \
    src/importvicedialog.cpp \
    src/importcharsetwidget.cpp

HEADERS  += src/mainwindow.h \
    src/state.h \
    src/aboutdialog.h \
    src/stateimport.h \
    src/stateexport.h \
    src/exportdialog.h \
    src/tilepropertiesdialog.h \
    src/commands.h \
    src/palette.h \
    src/bigcharwidget.h \
    src/charsetwidget.h \
    src/colorrectwidget.h \
    src/palettewidget.h \
    src/xlinkpreview.h \
    src/tilesetwidget.h \
    src/vchar64application.h \
    src/importvicedialog.h \
    src/importcharsetwidget.h

FORMS    += src/mainwindow.ui \
    src/aboutdialog.ui \
    src/exportdialog.ui \
    src/tilepropertiesdialog.ui \
    src/importvicedialog.ui

INCLUDEPATH += src

DISTFILES += \
    res/vchar64-icon-mac.icns

RESOURCES += \
    res/resources.qrc

QMAKE_CXXFLAGS += -Werror

TRANSLATIONS = translations/vchar64_es.ts

macx {
    TARGET = VChar64
    ICON = res/vchar64-icon-mac.icns
    QMAKE_INFO_PLIST = Info.plist
}
