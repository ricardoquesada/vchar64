TEMPLATE = app

QT += qml quick widgets

SOURCES += \
    src/main.cpp

RESOURCES += \
    src/qml.qrc

# Additional import path used to resolve QML modules in Qt Creator's code model
QML_IMPORT_PATH =

# Default rules for deployment.
include(deployment.pri)

DISTFILES += \
    src/c64-chargen.json \
    src/main.qml \
    src/MainForm.ui.qml \
    src/BigChar.qml
