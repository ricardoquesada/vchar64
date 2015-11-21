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

#include <QTranslator>
#include <QLibraryInfo>

#include "mainwindow.h"
#include "vchar64application.h"

int main(int argc, char *argv[])
{
    Q_INIT_RESOURCE(resources);

    VChar64Application app(argc, argv);

    // translation code
    QTranslator qtTranslator;
    qtTranslator.load("qt_" + QLocale::system().name(),
                      QLibraryInfo::location(QLibraryInfo::TranslationsPath));
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
    myappTranslator.load("vchar64_" + QLocale::system().name(),
                         translationDir);
    app.installTranslator(&myappTranslator);

    // name code
    app.setOrganizationDomain(QLatin1String("retro.moe"));
    app.setApplicationName(QLatin1String("VChar64"));
    app.setApplicationVersion(QLatin1String(GIT_VERSION));

    app.setApplicationDisplayName(QLatin1String("VChar64"));

#ifdef Q_OS_MAC
    app.setAttribute(Qt::AA_DontShowIconsInMenus);
#endif

#if QT_VERSION >= 0x050100
    // Enable support for highres images (added in Qt 5.1, but off by default)
    app.setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication::setWindowIcon(QIcon(":/res/logo512.png"));

    MainWindow* mainWin = MainWindow::getInstance();
    mainWin->show();

    QObject::connect(&app, &VChar64Application::fileOpenRequest, mainWin, &MainWindow::openFile);

    return app.exec();
}
