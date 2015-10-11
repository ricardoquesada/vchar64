#-------------------------------------------------
#
# Project created by QtCreator 2015-01-19T09:26:40
#
#-------------------------------------------------

QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = vchar64
TEMPLATE = app
VERSION = 0.0.8-git
DEFINES += APP_VERSION=\\\"$$VERSION\\\"

CONFIG += c++11

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
    src/tilesetwidget.cpp

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
    src/tilesetwidget.h

FORMS    += src/mainwindow.ui \
    src/aboutdialog.ui \
    src/exportdialog.ui \
    src/tilepropertiesdialog.ui

INCLUDEPATH += src

DISTFILES +=

RESOURCES += \
    res/resources.qrc

QMAKE_CXXFLAGS += -Werror

macx {
    ICON = res/vchar64.icns
}
