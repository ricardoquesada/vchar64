#-------------------------------------------------
#
# Project created by QtCreator 2015-01-19T09:26:40
#
#-------------------------------------------------

QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = editor
TEMPLATE = app


SOURCES += main.cpp\
        mainwindow.cpp \
    glwidget.cpp \
    helper.cpp \
    bigchar.cpp

HEADERS  += mainwindow.h \
    glwidget.h \
    helper.h \
    bigchar.h

FORMS    += mainwindow.ui
