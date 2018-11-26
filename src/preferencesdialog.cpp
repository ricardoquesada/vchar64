/****************************************************************************
Copyright 2016 Ricardo Quesada

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

#include "preferencesdialog.h"
#include "ui_preferencesdialog.h"

#include <QColorDialog>

#include "autoupdater.h"
#include "preferences.h"


PreferencesDialog::PreferencesDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::PreferencesDialog)
{
    ui->setupUi(this);

    connect(ui->toolButtonSelectColor, &QToolButton::clicked, this, &PreferencesDialog::onSelectColor);
    connect(ui->pushButtonCheckNow, &QPushButton::clicked, this, &PreferencesDialog::onUpdateNow);
    connect(ui->checkBoxStartupFiles, &QCheckBox::toggled, this, &PreferencesDialog::onStartUpFiles);
    connect(ui->checkBoxAutoCheck, &QCheckBox::toggled, this, &PreferencesDialog::onAutoCheckUpdates);
    auto& updateInstance = AutoUpdater::getInstance();
    connect(&updateInstance, &AutoUpdater::updateCheckFinished, this, &PreferencesDialog::onUpdateCheckFinished);

    setGridColor(Preferences::getInstance().getGridColor());
    ui->checkBoxStartupFiles->setChecked(Preferences::getInstance().getOpenLastFiles());
    ui->checkBoxAutoCheck->setChecked(Preferences::getInstance().getCheckUpdates());

    onUpdateCheckFinished();
}

PreferencesDialog::~PreferencesDialog()
{
    delete ui;
}

// from here: http://stackoverflow.com/a/15081186
void PreferencesDialog::setGridColor(const QColor &color)
{
    QSize iconSize = ui->toolButtonSelectColor->iconSize();
    QPixmap px(iconSize-QSize(2,2));
    px.fill(color);
    ui->toolButtonSelectColor->setIcon(px);

    Preferences::getInstance().setGridColor(color);
}

// slots
void PreferencesDialog::onSelectColor()
{
    QColor color = QColorDialog::getColor();
    if (color.isValid())
        setGridColor(color);
}

void PreferencesDialog::onStartUpFiles(bool checked)
{
    Preferences::getInstance().setOpenLastFiles(checked);
}

void PreferencesDialog::onAutoCheckUpdates(bool checked)
{
    Preferences::getInstance().setCheckUpdates(checked);
}

void PreferencesDialog::onUpdateNow()
{
    AutoUpdater::getInstance().checkUpdate();
}

void PreferencesDialog::onUpdateCheckFinished()
{
    QDateTime lastCheck = Preferences::getInstance().getLastUpdateCheckDate();
    ui->labelLastCheck->setText(tr("Last check: %1").arg(lastCheck.toString("ddd MMMM d yyyy - hh:mm:ss ap")));
}
