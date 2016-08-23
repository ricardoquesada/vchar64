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
#include <QRegExp>
#include <QVersionNumber>

#include "preferences.h"
#include "updatedialog.h"

AutoUpdater& AutoUpdater::getInstance()
{
    static AutoUpdater instance;
    return instance;
}

AutoUpdater::AutoUpdater()
    :_reply(nullptr)
    ,_httpRequestAborted(false)
    ,_inProgress(false)
{
    _url = QUrl::fromUserInput("https://raw.githubusercontent.com/ricardoquesada/vchar64/master/LATEST_VERSION.txt");
    Q_ASSERT(_url.isValid() && "Invalid URL");
}

AutoUpdater::~AutoUpdater()
{
}

void AutoUpdater::checkUpdate()
{
    if (_inProgress)
        return;

    _data = "";
    _inProgress = true;
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
    _inProgress = false;

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

    QString newVersion("");
    QString url("");
    QString changes("");

    auto list = _data.split("\n", QString::SkipEmptyParts);
    for(const auto str: list) {
        // not a comment?
        if (!str.startsWith("#")) {
            if (str.startsWith("stable_version:")) {
                newVersion = str.mid(sizeof("stable_version:")-1).trimmed();
            } else if (str.startsWith("stable_url:")) {
                url = str.mid(sizeof("stable_url:")-1).trimmed();
            } else if (str.startsWith("* ")) {
                changes.append(str.mid(sizeof("* ")-1).trimmed());
                changes.append("\n");
            }
        }
    }

    QDateTime currentTime = QDateTime::currentDateTime();
    Preferences::getInstance().setLastUpdateCheckDate(currentTime);

    QVersionNumber newVersionNumber = QVersionNumber::fromString(newVersion);
    QVersionNumber curVersionNumber = QVersionNumber::fromString(VERSION);

    if (newVersionNumber > curVersionNumber) {
        UpdateDialog dialog;
        dialog.setChanges(changes);
        dialog.setNewVersion(newVersion);
        dialog.exec();
    }

    emit updateCheckFinished();
}

void AutoUpdater::httpReadyRead()
{
    // this slot gets called every time the QNetworkReply has new data.
    // We read all of its new data and write it into the file.
    // That way we use less RAM than when reading it at the finished()
    // signal of the QNetworkReply
    _data.append(_reply->readAll());
}
