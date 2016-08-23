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

#include <QObject>
#include <QUrl>
#include <QNetworkAccessManager>

class AutoUpdater: public QObject
{
    Q_OBJECT
public:

    static AutoUpdater& getInstance();

    AutoUpdater(AutoUpdater const&)     = delete;
    void operator=(AutoUpdater const&)  = delete;

    void checkUpdate();
    void cancelDownload();

private slots:
    void httpFinished();
    void httpReadyRead();

private:
    AutoUpdater();
    ~AutoUpdater();

    void startRequest(const QUrl &requestedUrl);

    QUrl _url;
    QNetworkAccessManager _qnam;
    QNetworkReply *_reply;
    bool _httpRequestAborted;
};
