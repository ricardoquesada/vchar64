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
 * @file updatedialog.h
 * @brief Modal release-update presentation dialog (`vchar64` GUI target).
 */

#pragma once

#include <QDialog>
#include <QUrl>

#include "autoupdater.h"

namespace Ui {
class UpdateDialog;
}

/**
 * @brief Displays sanitized release notes and opens HTTPS-verified download URLs.
 *
 * Renders release notes strictly via `QTextBrowser::setPlainText()` (never `setHtml()`)
 * and re-verifies `AutoUpdater::isStrictHttpsUrl()` before invoking `QDesktopServices::openUrl()`.
 */
class UpdateDialog : public QDialog {
    Q_OBJECT

public:
    explicit UpdateDialog(QWidget* parent = nullptr);
    explicit UpdateDialog(const UpdateInfo& info, QWidget* parent = nullptr);
    ~UpdateDialog() override;

    /** @brief Populates version, plaintext changelog, and validated HTTPS download URL from `info`. */
    void setUpdateInfo(const UpdateInfo& info);
    /** @brief Renders `plainTextChanges` as plain text (rich-text/HTML rendering disabled). */
    void setChanges(const QString& plainTextChanges);
    void setNewVersion(const QString& newVersion);
    /** @brief Stores `url` and enables the Download button only when `AutoUpdater::isStrictHttpsUrl(url)` holds. */
    void setUpdateURL(const QUrl& url);
    void setUpdateURL(const QString& url);

private slots:
    void on_pushButtonDownload_clicked();
    void on_pushButtonLater_clicked();

private:
    Ui::UpdateDialog* ui;
    QUrl _url;
};
