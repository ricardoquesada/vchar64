#-------------------------------------------------
#
# Project created by QtCreator 2015-01-19T09:26:40
#
#-------------------------------------------------

QT       += core gui network widgets

# Taken from Qt Creator project files
defineTest(minQtVersion) {
    maj = $$1
    min = $$2
    patch = $$3
    isEqual(QT_MAJOR_VERSION, $$maj) {
        isEqual(QT_MINOR_VERSION, $$min) {
            isEqual(QT_PATCH_VERSION, $$patch) {
                return(true)
            }
            greaterThan(QT_PATCH_VERSION, $$patch) {
                return(true)
            }
        }
        greaterThan(QT_MINOR_VERSION, $$min) {
            return(true)
        }
    }
    greaterThan(QT_MAJOR_VERSION, $$maj) {
        return(true)
    }
    return(false)
}

!minQtVersion(5, 7, 0) {
    message("Cannot build VChar64 with Qt version $${QT_VERSION}")
    error("Use at least Qt 5.7.0.")
}

TARGET = vchar64
target.path = $${PREFIX}/bin
INSTALLS += target
win32 {
    DESTDIR = ..
} else {
    DESTDIR = ../bin
}
TEMPLATE = app
VERSION = 0.2.5
GIT_VERSION = $$system(git describe --abbrev=4 --dirty --always --tags)
DEFINES += GIT_VERSION=\\\"$$GIT_VERSION\\\" VERSION=\\\"$$VERSION\\\"

CONFIG += c++17
CONFIG += debug_and_release


SOURCES += \
    aboutdialog.cpp \
    autoupdater.cpp \
    bigcharwidget.cpp \
    charsetwidget.cpp \
    colorrectwidget.cpp \
    commands.cpp \
    exportdialog.cpp \
    exportimagedialog.cpp \
    fileutils.cpp \
    importkoalabitmapwidget.cpp \
    importkoalacharsetwidget.cpp \
    importkoaladialog.cpp \
    importvicecharsetwidget.cpp \
    importvicedialog.cpp \
    importvicescreenramwidget.cpp \
    main.cpp\
    mainwindow.cpp \
    mappropertiesdialog.cpp \
    mapwidget.cpp \
    palette.cpp \
    palettewidget.cpp \
    preferences.cpp \
    preferencesdialog.cpp \
    selectcolordialog.cpp \
    serverconnectdialog.cpp \
    serverpreview.cpp \
    state.cpp \
    stateexport.cpp \
    stateimport.cpp \
    tilepropertiesdialog.cpp \
    tilesetwidget.cpp \
    updatedialog.cpp \
    utils.cpp \
    vchar64application.cpp \
    xlinkpreview.cpp

HEADERS  += \
    aboutdialog.h \
    autoupdater.h \
    bigcharwidget.h \
    charsetwidget.h \
    colorrectwidget.h \
    commands.h \
    exportdialog.h \
    exportimagedialog.h \
    fileutils.h \
    importkoalabitmapwidget.h \
    importkoalacharsetwidget.h \
    importkoaladialog.h \
    importvicecharsetwidget.h \
    importvicedialog.h \
    importvicescreenramwidget.h \
    mainwindow.h \
    mappropertiesdialog.h \
    mapwidget.h \
    palette.h \
    palettewidget.h \
    preferences.h \
    preferencesdialog.h \
    selectcolordialog.h \
    serverconnectdialog.h \
    serverpreview.h \
    serverprotocol.h \
    state.h \
    stateexport.h \
    stateimport.h \
    tilepropertiesdialog.h \
    tilesetwidget.h \
    updatedialog.h \
    utils.h \
    vchar64application.h \
    xlinkpreview.h

FORMS    += \
    aboutdialog.ui \
    exportdialog.ui \
    exportimagedialog.ui \
    importkoaladialog.ui \
    importvicedialog.ui \
    mainwindow.ui \
    mappropertiesdialog.ui \
    preferencesdialog.ui \
    selectcolordialog.ui \
    serverconnectdialog.ui \
    tilepropertiesdialog.ui \
    updatedialog.ui

INCLUDEPATH += src

DISTFILES += \
    res/vchar64-icon-mac.icns

RESOURCES += \
    resources.qrc

# see bug: https://github.com/ricardoquesada/vchar64/issues/61
#!win32 {
#    QMAKE_CXXFLAGS += -Werror
#}

win32 {
    RC_FILE = res/vchar64.rc
}
macx {
    TARGET = VChar64
    ICON = res/vchar64-icon-mac.icns
    QMAKE_INFO_PLIST = res/Info.plist
}
