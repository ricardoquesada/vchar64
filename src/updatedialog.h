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

#pragma once

#include <QDialog>
#include <QUrl>

namespace Ui {
class UpdateDialog;
}

class UpdateDialog : public QDialog {
    Q_OBJECT

public:
    explicit UpdateDialog(QWidget* parent = nullptr);
    virtual ~UpdateDialog() override;

    void setChanges(const QString& changes);
    void setNewVersion(const QString& newVersion);
    void setUpdateURL(const QString& url);

private slots:
    void on_pushButtonDownload_clicked();

    void on_pushButtonLater_clicked();

private:
    Ui::UpdateDialog* ui;
    QUrl _url;
};
