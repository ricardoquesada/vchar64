/****************************************************************************
Copyright 2024 Ricardo Quesada

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

#include "exportimagedialog.h"
#include "ui_exportimagedialog.h"
#include "ui_mainwindow.h"

#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QStatusBar>
#include <QWidget>

#include "mainwindow.h"
#include "state.h"
#include "preferences.h"

ExportImageDialog::ExportImageDialog(State* state, QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::ExportImageDialog)
    , _state(state)
{
    ui->setupUi(this);

    const auto lastDir = Preferences::getInstance().getLastUsedDirectory();

           // set correct extension
    const auto exportProperties = _state->getExportProperties();
    const int format = exportProperties.format;

    auto fn = _state->getExportedFilename();
    if (fn.length() == 0) {
        fn = state->getLoadedFilename();

       // if getExportedFilename() == 0 then getLoadedFilename() will have the .vcharproj extension.
       // Replace it with .png
        if (fn.length() != 0) {
            QFileInfo fileInfo(fn);
            fn = fileInfo.absolutePath() + "/" + fileInfo.completeBaseName();
            fn += ".png";
        }
    }

    if (fn.length() == 0)
        fn = lastDir + "/" + "untitled";

    ui->editFilename->setText(fn);

}

ExportImageDialog::~ExportImageDialog()
{
    delete ui;
}

void ExportImageDialog::on_pushBrowse_clicked()
{
    QString filters[] = {
        tr("PNG files (*.png)"),
    };

    int filterIdx = 0;

    auto filename = QFileDialog::getSaveFileName(this,
        tr("Select filename"),
        ui->editFilename->text(),
        tr("PNG files (*.png);;Any file (*)"),
        &filters[filterIdx],
        QFileDialog::DontConfirmOverwrite);

    if (!filename.isEmpty())
        ui->editFilename->setText(filename);
}

void ExportImageDialog::accept()
{
    bool ok = false;
    MapWidget* mapWidget = nullptr;
    TilesetWidget* tilesetWidget = nullptr;
    auto filename = ui->editFilename->text();
    auto mainWindow = qobject_cast<MainWindow*>(parent());


    auto properties = _state->getExportProperties();

    if (ui->checkBox_map->isChecked())
        mapWidget = mainWindow->getUi()->mapWidget;
    if (ui->checkBox_tileset->isChecked())
        tilesetWidget = mainWindow->getUi()->tilesetWidget;

    ok = _state->exportPNG(filename, tilesetWidget, mapWidget);

    if (ok) {
        QFileInfo info(filename);
        auto dir = info.absolutePath();
        Preferences::getInstance().setLastUsedDirectory(dir);
        mainWindow->statusBar()->showMessage(tr("File exported to %1").arg(_state->getExportedFilename()), 2000);
    } else {
        mainWindow->statusBar()->showMessage(tr("Export failed"), 2000);
        qDebug() << "Error saving file: " << filename;
    }

    QDialog::accept();
}
