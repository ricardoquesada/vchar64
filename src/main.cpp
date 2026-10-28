/****************************************************************************
Copyright 2015 Ricardo Quesada

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
****************************************************************************/

/**
 * @file main.cpp
 * @brief Desktop GUI executable entrypoint (`vchar64` target).
 *
 * Initializes embedded Qt resources (`resources.qrc`, including `:/i18n` `.qm`
 * translations), configures macOS Finder file-open event routing via
 * `VChar64Application`, restores persisted window layout, and opens either the
 * CLI project path or the default C64 Upper/Graphics document.
 */

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

#include "mainwindow.h"
#include "vchar64application.h"

int main(int argc, char* argv[])
{
    // Initialize resources explicitly so embedded assets and translations load
    // reliably even when helper targets are linked as static archives.
    Q_INIT_RESOURCE(resources);

    VChar64Application app(argc, argv);

    // translation code
    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale::system(), QStringLiteral("qt"), QStringLiteral("_"),
            QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtTranslator);

    QTranslator myappTranslator;
    if (myappTranslator.load(QLocale::system(), QStringLiteral("vchar64"), QStringLiteral("_"),
            QStringLiteral(":/i18n")))
        app.installTranslator(&myappTranslator);

    // name code
    app.setOrganizationDomain(QLatin1String("retro.moe"));
    app.setApplicationName(QLatin1String("VChar64"));
    QCoreApplication::setApplicationVersion(QStringLiteral(VCHAR64_VERSION));
    app.setApplicationDisplayName(QLatin1String("VChar64"));

#ifdef Q_OS_MACOS
    app.setAttribute(Qt::AA_DontShowIconsInMenus);
#endif

    QApplication::setWindowIcon(QIcon(":/res/logo512.png"));

    MainWindow* mainWin = MainWindow::getInstance();

    // FIXME: readSettings() should be outside the constructor to prevent a recursion
    // when the docks are floating
    mainWin->readSettings();

    mainWin->show();

    QObject::connect(&app, &VChar64Application::fileOpenRequest, mainWin, &MainWindow::openFile);

    bool loadFromArgv = false;
    if (argc == 2) {
        if (QFile::exists(argv[1]))
            loadFromArgv = true;
        else
            qDebug() << "Invalid VChar project file: " << argv[1];
    }
    if (loadFromArgv)
        mainWin->openFile(argv[1]);
    else
        mainWin->openDefaultDocument();

    return app.exec();
}
