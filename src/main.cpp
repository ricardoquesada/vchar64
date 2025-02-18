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

#include <QDebug>
#include <QLibraryInfo>
#include <QTranslator>

#include "mainwindow.h"
#include "vchar64application.h"

int main(int argc, char* argv[])
{
    Q_INIT_RESOURCE(resources);

    VChar64Application app(argc, argv);

    // translation code
    QTranslator qtTranslator;
    if (qtTranslator.load("qt_" + QLocale::system().name(),
#if QT_VERSION_MAJOR >= 6
            QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
#else
            QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
#endif
        app.installTranslator(&qtTranslator);

    auto translationDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN32
    translationDir += QLatin1String("/translations");
#elif defined(Q_OS_MAC)
    translationDir += QLatin1String("/../Translations");
#else
    translationDir += QLatin1String("/../share/tiled/translations");
#endif

    QTranslator myappTranslator;
    if (myappTranslator.load("vchar64_" + QLocale::system().name(),
            translationDir))
        app.installTranslator(&myappTranslator);

    // name code
    app.setOrganizationDomain(QLatin1String("retro.moe"));
    app.setApplicationName(QLatin1String("VChar64"));

    // if compiled from .tar.gz, GIT_VERSION will be empty
    // FIXME: this should be evaluated as a preprocessor macro
    if (GIT_VERSION && strlen(GIT_VERSION) != 0)
        app.setApplicationVersion(QLatin1String(GIT_VERSION));
    else
        app.setApplicationVersion(QLatin1String(VERSION));

    app.setApplicationDisplayName(QLatin1String("VChar64"));

#ifdef Q_OS_MAC
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
