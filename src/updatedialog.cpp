/****************************************************************************
Copyright 2016-2026 Ricardo Quesada

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
 * @file updatedialog.cpp
 * @brief Implementation of `UpdateDialog` (`vchar64` GUI target).
 */

#include "updatedialog.h"
#include "ui_updatedialog.h"

#include <QDebug>
#include <QDesktopServices>

UpdateDialog::UpdateDialog(QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::UpdateDialog)
{
    ui->setupUi(this);

    ui->labelCurrentVersion->setText(
        tr("Current version: %1").arg(QStringLiteral(VCHAR64_VERSION)));
    ui->pushButtonDownload->setEnabled(false);
}

UpdateDialog::UpdateDialog(const UpdateInfo& info, QWidget* parent)
    : UpdateDialog(parent)
{
    setUpdateInfo(info);
}

UpdateDialog::~UpdateDialog()
{
    delete ui;
}

void UpdateDialog::setUpdateInfo(const UpdateInfo& info)
{
    setNewVersion(info.version);
    setChanges(info.plainTextChanges);
    setUpdateURL(info.httpsDownloadUrl);
}

void UpdateDialog::setNewVersion(const QString& newVersion)
{
    ui->labelNewVersion->setText(tr("New version: %1").arg(newVersion));
}

void UpdateDialog::setChanges(const QString& plainTextChanges)
{
    // Render exclusively as plain text so untrusted manifest payloads cannot inject
    // HTML, external `<img>` fetches, or rich-text links into `QTextBrowser`.
    ui->textBrowserChanges->setPlainText(plainTextChanges);
}

void UpdateDialog::setUpdateURL(const QUrl& url)
{
    if (AutoUpdater::isStrictHttpsUrl(url)) {
        _url = url;
        ui->pushButtonDownload->setEnabled(true);
    } else {
        _url.clear();
        ui->pushButtonDownload->setEnabled(false);
        qWarning() << "Rejected unsafe update download URL:" << url;
    }
}

void UpdateDialog::setUpdateURL(const QString& url)
{
    setUpdateURL(QUrl(url.trimmed(), QUrl::StrictMode));
}

void UpdateDialog::on_pushButtonDownload_clicked()
{
    // Defense-in-depth scheme check immediately before handing `_url` to the OS shell;
    // blocks `file://`, `javascript:`, `smb://`, and `http://` URIs unconditionally.
    if (!AutoUpdater::isStrictHttpsUrl(_url)) {
        qWarning() << "Blocked non-HTTPS download URL launch:" << _url;
        return;
    }
    QDesktopServices::openUrl(_url);
    accept();
}

void UpdateDialog::on_pushButtonLater_clicked()
{
    reject();
}
