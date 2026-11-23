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

// Implementation of the hardened HTTPS release-manifest checker (vchar64_net).
//
// Enforces NoLessSafeRedirectPolicy, a 64 KiB streaming response budget,
// 10-second inactivity/transfer timeouts, and tag-stripping HTML-to-plaintext
// changelog conversion before emitting UpdateInfo to GUI listeners.

#include "autoupdater.h"

#include <algorithm>

#include <QIODevice>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVersionNumber>

namespace {

constexpr QLatin1String kVersionPrefix("stable_version:");
constexpr QLatin1String kUrlPrefix("stable_url:");
constexpr QLatin1String kChangesPrefix("changes:");

} // namespace

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

void AutoUpdater::setTransferTimeoutMs(int timeout_ms) noexcept
{
    _transferTimeoutMs = std::max(1, timeout_ms);
}

void AutoUpdater::checkUpdate()
{
    checkUpdate(_url);
}

void AutoUpdater::checkUpdate(const QUrl& requested_url)
{
    if (_inProgress) {
        return;
    }

    if (!isStrictHttpsUrl(requested_url)) {
        failCheck(tr("Rejected non-HTTPS update URL: %1").arg(requested_url.toString()));
        return;
    }

    beginStreamingCheck();
    startRequest(requested_url);
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

void AutoUpdater::beginStreamingCheck() noexcept
{
    _rawResponse.clear();
    _httpRequestAborted = false;
    _oversizeAborted = false;
    _timedOut = false;
    _inProgress = true;
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
            // Detach `_reply` signals BEFORE calling `abort()` because
            // `QNetworkReply::abort()` synchronously emits `finished()` on the
            // current call stack; disconnecting first prevents re-entrant
            // double `failCheck()` emission.
            QNetworkReply* active_reply = _reply;
            _reply = nullptr;
            active_reply->disconnect(this);
            active_reply->abort();
            active_reply->deleteLater();
        }

        failCheck(tr("Update manifest exceeded maximum allowed size of 64 KiB."));
        return false;
    }

    _rawResponse.append(chunk.data(), chunk.size());
    return true;
}

void AutoUpdater::processManifestPayload(const QByteArray& utf8_manifest)
{
    _inProgress = false;

    if (utf8_manifest.size() > kMaxResponseBytes) {
        failCheck(tr("Update manifest exceeded maximum allowed size of 64 KiB."));
        return;
    }

    QString manifest_text = QString::fromUtf8(utf8_manifest);
    manifest_text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    manifest_text.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    QString raw_version;
    QString raw_url;
    QStringList raw_changes_lines;
    bool in_changes = false;

    const QStringList lines = manifest_text.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1Char('#'))) {
            continue;
        }

        if (in_changes) {
            raw_changes_lines.append(line);
            continue;
        }

        if (trimmed.isEmpty()) {
            continue;
        }
        if (trimmed.startsWith(kVersionPrefix)) {
            raw_version = trimmed.mid(kVersionPrefix.size()).trimmed();
        } else if (trimmed.startsWith(kUrlPrefix)) {
            raw_url = trimmed.mid(kUrlPrefix.size()).trimmed();
        } else if (trimmed.startsWith(kChangesPrefix)) {
            in_changes = true;
            const QString inline_changes = trimmed.mid(kChangesPrefix.size()).trimmed();
            if (!inline_changes.isEmpty()) {
                raw_changes_lines.append(inline_changes);
            }
        }
    }

    const QVersionNumber new_version = QVersionNumber::fromString(raw_version);
    const QVersionNumber current_version = QVersionNumber::fromString(_currentVersion);
    if (raw_version.isEmpty() || new_version.isNull() || current_version.isNull()) {
        failCheck(tr("Invalid or missing version number in update manifest."));
        return;
    }

    if (new_version <= current_version) {
        emit upToDate();
        emit updateCheckFinished();
        return;
    }

    const QUrl parsed_download_url(raw_url, QUrl::StrictMode);
    if (raw_url.isEmpty() || !isStrictHttpsUrl(parsed_download_url)) {
        failCheck(tr("Rejected non-HTTPS or invalid update download URL."));
        return;
    }

    UpdateInfo info;
    info.version = raw_version;
    info.plainTextChanges = sanitizeChangelogHtmlToPlainText(
        raw_changes_lines.join(QLatin1Char('\n')));
    info.httpsDownloadUrl = parsed_download_url;

    emit updateAvailable(info);
    emit updateCheckFinished();
}

bool AutoUpdater::isStrictHttpsUrl(const QUrl& url) noexcept
{
    return url.isValid()
        && !url.isRelative()
        && url.scheme().toLower() == QLatin1String("https")
        && !url.host().isEmpty();
}

QString AutoUpdater::sanitizeChangelogHtmlToPlainText(const QString& raw_changes)
{
    if (raw_changes.isEmpty()) {
        return { };
    }

    QString text = raw_changes;
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    // Strip embedded <script>...</script> and <style>...</style> blocks completely.
    const QRegularExpression script_or_style_re(
        QStringLiteral("<(script|style)\\b[^>]*>.*?</\\1\\s*>"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    text.replace(script_or_style_re, QStringLiteral("\n"));

    // Preserve line breaks for HTML break and block-closing tags.
    const QRegularExpression break_tag_re(
        QStringLiteral("<br\\s*/?>"),
        QRegularExpression::CaseInsensitiveOption);
    text.replace(break_tag_re, QStringLiteral("\n"));

    const QRegularExpression block_close_tag_re(
        QStringLiteral("</(p|li|div|tr|h[1-6])\\s*>"),
        QRegularExpression::CaseInsensitiveOption);
    text.replace(block_close_tag_re, QStringLiteral("\n"));

    // Convert <li> list items into plain-text "- " bullets.
    const QRegularExpression list_item_open_tag_re(
        QStringLiteral("<li(?:\\s[^>]*)?>"),
        QRegularExpression::CaseInsensitiveOption);
    text.replace(list_item_open_tag_re, QStringLiteral("\n- "));

    // Strip all remaining HTML/XML tags and comments.
    const QRegularExpression any_html_tag_re(
        QStringLiteral("<[A-Za-z!/?][^>]*>"));
    text.remove(any_html_tag_re);

    // Decode common safe non-markup entities in plain text.
    text.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "), Qt::CaseInsensitive);
    text.replace(QStringLiteral("&quot;"), QStringLiteral("\""), Qt::CaseInsensitive);
    text.replace(QStringLiteral("&#39;"), QStringLiteral("'"), Qt::CaseInsensitive);
    text.replace(QStringLiteral("&apos;"), QStringLiteral("'"), Qt::CaseInsensitive);
    text.replace(QStringLiteral("&amp;"), QStringLiteral("&"), Qt::CaseInsensitive);

    const QRegularExpression bullet_spacing_re(QStringLiteral("^-\\s+"));
    QStringList cleaned_lines;
    const QStringList raw_lines = text.split(QLatin1Char('\n'));
    cleaned_lines.reserve(raw_lines.size());

    for (const QString& raw_line : raw_lines) {
        QString line = raw_line.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        line.replace(bullet_spacing_re, QStringLiteral("- "));
        if (line == QStringLiteral("-")) {
            continue;
        }
        cleaned_lines.append(line);
    }

    return cleaned_lines.join(QLatin1Char('\n'));
}

void AutoUpdater::httpFinished()
{
    _timeoutTimer.stop();

    QNetworkReply* finished_reply = _reply;
    _reply = nullptr;

    if (finished_reply == nullptr) {
        _inProgress = false;
        return;
    }

    const bool was_cancelled = _httpRequestAborted;
    const bool was_oversize = _oversizeAborted;
    const bool was_timed_out = _timedOut;

    if (was_cancelled || was_oversize) {
        _inProgress = false;
        _rawResponse.clear();
        finished_reply->deleteLater();
        return;
    }

    if (was_timed_out || finished_reply->error() != QNetworkReply::NoError) {
        const QString error_message = was_timed_out
            ? tr("Update check timed out after %1 ms.").arg(_transferTimeoutMs)
            : finished_reply->errorString();
        _rawResponse.clear();
        finished_reply->deleteLater();
        failCheck(error_message);
        return;
    }

    if (!isStrictHttpsUrl(finished_reply->url())) {
        _rawResponse.clear();
        finished_reply->deleteLater();
        failCheck(tr("Update check redirected to non-HTTPS endpoint."));
        return;
    }

    const QByteArray trailing = finished_reply->readAll();
    finished_reply->deleteLater();

    if (!ingestResponseChunk(trailing)) {
        return;
    }

    const QByteArray complete_payload = _rawResponse;
    _rawResponse.clear();
    _inProgress = false;

    processManifestPayload(complete_payload);
}

void AutoUpdater::httpReadyRead()
{
    if (_reply == nullptr || _httpRequestAborted || _oversizeAborted) {
        return;
    }

    _timeoutTimer.start(_transferTimeoutMs);
    const QByteArray chunk = _reply->readAll();
    static_cast<void>(ingestResponseChunk(chunk));
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

void AutoUpdater::startRequest(const QUrl& requested_url)
{
    _qnam.clearAccessCache();

    QNetworkRequest request(requested_url);
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

void AutoUpdater::failCheck(const QString& reason)
{
    _inProgress = false;
    _rawResponse.clear();
    emit checkFailed(reason);
    emit updateCheckFinished();
}
