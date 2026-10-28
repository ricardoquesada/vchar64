/****************************************************************************
Copyright 2015-2026 Ricardo Quesada

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
 * @file autoupdater.cpp
 * @brief Implementation of the hardened HTTPS release-manifest checker (`vchar64_net`).
 *
 * Enforces `NoLessSafeRedirectPolicy`, a `64 KiB` streaming response budget,
 * `10`-second inactivity/transfer timeouts, and tag-stripping HTML-to-plaintext
 * changelog conversion before emitting `UpdateInfo` to GUI listeners.
 */

#include "autoupdater.h"

#include <algorithm>

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStringList>
#include <QVersionNumber>

AutoUpdater& AutoUpdater::getInstance()
{
    static AutoUpdater instance;
    return instance;
}

AutoUpdater::AutoUpdater(QObject* parent)
    : QObject(parent)
    , _url(QUrl(QString::fromLatin1(kDefaultManifestUrl), QUrl::StrictMode))
    , _currentVersion(QStringLiteral(VCHAR64_VERSION))
{
    qRegisterMetaType<UpdateInfo>();

    _timeoutTimer.setSingleShot(true);
    connect(&_timeoutTimer, &QTimer::timeout, this, &AutoUpdater::onTransferTimeout);
}

AutoUpdater::~AutoUpdater()
{
    _timeoutTimer.stop();
    if (_reply != nullptr) {
        _reply->disconnect(this);
        _reply->abort();
        _reply->deleteLater();
        _reply = nullptr;
    }
}

void AutoUpdater::setManifestUrl(const QUrl& url)
{
    _url = url;
}

void AutoUpdater::setCurrentVersion(const QString& version)
{
    _currentVersion = version.trimmed();
}

void AutoUpdater::setTransferTimeoutMs(int timeoutMs) noexcept
{
    _transferTimeoutMs = std::max(1, timeoutMs);
}

bool AutoUpdater::isStrictHttpsUrl(const QUrl& url) noexcept
{
    return url.isValid()
        && !url.isRelative()
        && url.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) == 0
        && !url.host().isEmpty();
}

QString AutoUpdater::sanitizeChangelogHtmlToPlainText(const QString& rawChanges)
{
    if (rawChanges.isEmpty()) {
        return { };
    }

    QString text = rawChanges;
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    // Strip embedded <script>...</script> and <style>...</style> blocks completely.
    static const QRegularExpression kScriptOrStyleBlockRe(
        QStringLiteral("<(script|style)\\b[^>]*>.*?</\\1\\s*>"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    text.replace(kScriptOrStyleBlockRe, QStringLiteral("\n"));

    // Preserve line breaks for HTML break and block-closing tags.
    static const QRegularExpression kBreakTagRe(
        QStringLiteral("<br\\s*/?>"),
        QRegularExpression::CaseInsensitiveOption);
    text.replace(kBreakTagRe, QStringLiteral("\n"));

    static const QRegularExpression kBlockCloseTagRe(
        QStringLiteral("</(p|li|div|tr|h[1-6])\\s*>"),
        QRegularExpression::CaseInsensitiveOption);
    text.replace(kBlockCloseTagRe, QStringLiteral("\n"));

    // Convert <li> list items into plain-text "- " bullets.
    static const QRegularExpression kListItemOpenTagRe(
        QStringLiteral("<li(?:\\s[^>]*)?>"),
        QRegularExpression::CaseInsensitiveOption);
    text.replace(kListItemOpenTagRe, QStringLiteral("\n- "));

    // Strip all remaining HTML/XML tags and comments.
    static const QRegularExpression kAnyHtmlTagRe(
        QStringLiteral("<[A-Za-z!/?][^>]*>"));
    text.remove(kAnyHtmlTagRe);

    // Decode common safe non-markup entities in plain text.
    text.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "), Qt::CaseInsensitive);
    text.replace(QStringLiteral("&quot;"), QStringLiteral("\""), Qt::CaseInsensitive);
    text.replace(QStringLiteral("&#39;"), QStringLiteral("'"), Qt::CaseInsensitive);
    text.replace(QStringLiteral("&apos;"), QStringLiteral("'"), Qt::CaseInsensitive);
    text.replace(QStringLiteral("&amp;"), QStringLiteral("&"), Qt::CaseInsensitive);

    static const QRegularExpression kBulletSpacingRe(QStringLiteral("^-\\s+"));
    QStringList cleanedLines;
    const QStringList rawLines = text.split(QLatin1Char('\n'));
    cleanedLines.reserve(rawLines.size());

    for (const QString& rawLine : rawLines) {
        QString line = rawLine.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        line.replace(kBulletSpacingRe, QStringLiteral("- "));
        if (line == QStringLiteral("-")) {
            continue;
        }
        cleanedLines.append(line);
    }

    return cleanedLines.join(QLatin1Char('\n'));
}

void AutoUpdater::checkUpdate()
{
    checkUpdate(_url);
}

void AutoUpdater::checkUpdate(const QUrl& requestedUrl)
{
    if (_inProgress) {
        return;
    }

    if (!isStrictHttpsUrl(requestedUrl)) {
        failCheck(tr("Rejected non-HTTPS update URL: %1").arg(requestedUrl.toString()));
        return;
    }

    beginStreamingCheck();
    startRequest(requestedUrl);
}

void AutoUpdater::beginStreamingCheck() noexcept
{
    _rawResponse.clear();
    _httpRequestAborted = false;
    _oversizeAborted = false;
    _timedOut = false;
    _inProgress = true;
}

void AutoUpdater::startRequest(const QUrl& requestedUrl)
{
    _qnam.clearAccessCache();

    QNetworkRequest request(requestedUrl);
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(kMaxRedirects);
    request.setTransferTimeout(_transferTimeoutMs);

    _reply = _qnam.get(request);
    connect(_reply, &QIODevice::readyRead, this, &AutoUpdater::httpReadyRead);
    connect(_reply, &QNetworkReply::finished, this, &AutoUpdater::httpFinished);

    _timeoutTimer.start(_transferTimeoutMs);
}

void AutoUpdater::cancelDownload()
{
    _timeoutTimer.stop();
    _httpRequestAborted = true;
    _inProgress = false;
    _rawResponse.clear();
    if (_reply != nullptr) {
        _reply->abort();
    }
}

void AutoUpdater::onTransferTimeout()
{
    if (!_inProgress) {
        return;
    }
    _timedOut = true;
    if (_reply != nullptr) {
        _reply->abort();
    } else {
        failCheck(tr("Update check timed out after %1 ms.").arg(_transferTimeoutMs));
    }
}

bool AutoUpdater::ingestResponseChunk(QByteArrayView chunk)
{
    if (chunk.isEmpty()) {
        return true;
    }

    if (chunk.size() > (kMaxResponseBytes - _rawResponse.size())) {
        _timeoutTimer.stop();
        _oversizeAborted = true;
        _inProgress = false;
        _rawResponse.clear();

        if (_reply != nullptr) {
            // Detach `_reply` signals BEFORE calling `abort()` because `QNetworkReply::abort()`
            // synchronously emits `finished()` on the current call stack; disconnecting first
            // prevents re-entrant double `failCheck()` emission.
            QNetworkReply* activeReply = _reply;
            _reply = nullptr;
            activeReply->disconnect(this);
            activeReply->abort();
            activeReply->deleteLater();
        }

        failCheck(tr("Update manifest exceeded maximum allowed size of 64 KiB."));
        return false;
    }

    _rawResponse.append(chunk.data(), chunk.size());
    return true;
}

void AutoUpdater::httpReadyRead()
{
    if (_reply == nullptr || _httpRequestAborted || _oversizeAborted) {
        return;
    }

    _timeoutTimer.start(_transferTimeoutMs);
    const QByteArray chunk = _reply->readAll();
    (void)ingestResponseChunk(chunk);
}

void AutoUpdater::httpFinished()
{
    _timeoutTimer.stop();

    QNetworkReply* finishedReply = _reply;
    _reply = nullptr;

    if (finishedReply == nullptr) {
        _inProgress = false;
        return;
    }

    const bool wasCancelled = _httpRequestAborted;
    const bool wasOversize = _oversizeAborted;
    const bool wasTimedOut = _timedOut;

    if (wasCancelled || wasOversize) {
        _inProgress = false;
        _rawResponse.clear();
        finishedReply->deleteLater();
        return;
    }

    if (wasTimedOut || finishedReply->error() != QNetworkReply::NoError) {
        const QString errStr = wasTimedOut
            ? tr("Update check timed out after %1 ms.").arg(_transferTimeoutMs)
            : finishedReply->errorString();
        _rawResponse.clear();
        finishedReply->deleteLater();
        failCheck(errStr);
        return;
    }

    if (!isStrictHttpsUrl(finishedReply->url())) {
        _rawResponse.clear();
        finishedReply->deleteLater();
        failCheck(tr("Update check redirected to non-HTTPS endpoint."));
        return;
    }

    const QByteArray trailing = finishedReply->readAll();
    finishedReply->deleteLater();

    if (!ingestResponseChunk(trailing)) {
        return;
    }

    const QByteArray completePayload = _rawResponse;
    _rawResponse.clear();
    _inProgress = false;

    processManifestPayload(completePayload);
}

void AutoUpdater::processManifestPayload(const QByteArray& utf8Manifest)
{
    _inProgress = false;

    if (utf8Manifest.size() > kMaxResponseBytes) {
        failCheck(tr("Update manifest exceeded maximum allowed size of 64 KiB."));
        return;
    }

    QString manifestText = QString::fromUtf8(utf8Manifest);
    manifestText.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    manifestText.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    QString rawVersion;
    QString rawUrl;
    QStringList rawChangesLines;
    bool inChanges = false;

    static constexpr QLatin1String kVersionPrefix("stable_version:");
    static constexpr QLatin1String kUrlPrefix("stable_url:");
    static constexpr QLatin1String kChangesPrefix("changes:");

    const QStringList lines = manifestText.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1Char('#'))) {
            continue;
        }

        if (!inChanges) {
            if (trimmed.isEmpty()) {
                continue;
            }
            if (trimmed.startsWith(kVersionPrefix)) {
                rawVersion = trimmed.mid(kVersionPrefix.size()).trimmed();
            } else if (trimmed.startsWith(kUrlPrefix)) {
                rawUrl = trimmed.mid(kUrlPrefix.size()).trimmed();
            } else if (trimmed.startsWith(kChangesPrefix)) {
                inChanges = true;
                const QString inlineChanges = trimmed.mid(kChangesPrefix.size()).trimmed();
                if (!inlineChanges.isEmpty()) {
                    rawChangesLines.append(inlineChanges);
                }
            }
        } else {
            rawChangesLines.append(line);
        }
    }

    const QVersionNumber newVersionNumber = QVersionNumber::fromString(rawVersion);
    const QVersionNumber curVersionNumber = QVersionNumber::fromString(_currentVersion);
    if (rawVersion.isEmpty() || newVersionNumber.isNull() || curVersionNumber.isNull()) {
        failCheck(tr("Invalid or missing version number in update manifest."));
        return;
    }

    if (newVersionNumber <= curVersionNumber) {
        emit upToDate();
        emit updateCheckFinished();
        return;
    }

    const QUrl parsedDownloadUrl(rawUrl, QUrl::StrictMode);
    if (rawUrl.isEmpty() || !isStrictHttpsUrl(parsedDownloadUrl)) {
        failCheck(tr("Rejected non-HTTPS or invalid update download URL."));
        return;
    }

    UpdateInfo info;
    info.version = rawVersion;
    info.plainTextChanges = sanitizeChangelogHtmlToPlainText(
        rawChangesLines.join(QLatin1Char('\n')));
    info.httpsDownloadUrl = parsedDownloadUrl;

    emit updateAvailable(info);
    emit updateCheckFinished();
}

void AutoUpdater::failCheck(const QString& reason)
{
    _inProgress = false;
    _rawResponse.clear();
    emit checkFailed(reason);
    emit updateCheckFinished();
}
