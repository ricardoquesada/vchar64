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

#include "autoupdater.h"

#include <QNetworkRequest>
#include <QNetworkReply>

AutoUpdater& AutoUpdater::getInstance()
{
    static AutoUpdater instance;
    return instance;
}

AutoUpdater::AutoUpdater()
    :_reply(nullptr)
    ,_httpRequestAborted(false)
{
    _url = QUrl::fromUserInput("https://github.com/ricardoquesada/vchar64/blob/master/LATEST_VERSION.txt");
    Q_ASSERT(_url.isValid() && "Invalid URL");

}

AutoUpdater::~AutoUpdater()
{
}

void AutoUpdater::checkUpdate()
{
    _httpRequestAborted = false;
    startRequest(_url);
}

void AutoUpdater::startRequest(const QUrl &requestedUrl)
{
    _reply = _qnam.get(QNetworkRequest(requestedUrl));
    connect(_reply, &QNetworkReply::finished, this, &AutoUpdater::httpFinished);
    connect(_reply, &QIODevice::readyRead, this, &AutoUpdater::httpReadyRead);
}

//
// slots
//
void AutoUpdater::cancelDownload()
{
    qDebug() << "Download canceled.";
    _httpRequestAborted = true;
    _reply->abort();
}

void AutoUpdater::httpFinished()
{
    if (_httpRequestAborted) {
        _reply->deleteLater();
        _reply = Q_NULLPTR;
        return;
    }

    if (_reply->error()) {
        qDebug() << _reply->errorString();
        _reply->deleteLater();
        _reply = Q_NULLPTR;
        return;
    }

    const QVariant redirectionTarget = _reply->attribute(QNetworkRequest::RedirectionTargetAttribute);

    _reply->deleteLater();
    _reply = Q_NULLPTR;

    if (!redirectionTarget.isNull()) {
        const QUrl redirectedUrl = _url.resolved(redirectionTarget.toUrl());
        startRequest(redirectedUrl);
        return;
    }
}

void AutoUpdater::httpReadyRead()
{
    // this slot gets called every time the QNetworkReply has new data.
    // We read all of its new data and write it into the file.
    // That way we use less RAM than when reading it at the finished()
    // signal of the QNetworkReply
    qDebug() << _reply->readAll();
}
