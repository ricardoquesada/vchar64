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
 * @file autoupdater.h
 * @brief Headless HTTPS release-update checker and manifest sanitizer (`vchar64_net`).
 *
 * Implements defense-in-depth update verification without `Qt6::Widgets` or `QSettings`
 * coupling: rejects non-HTTPS schemes and HTTP downgrade redirects, caps response memory
 * at `64 KiB` during streaming, enforces a `10`-second transfer watchdog, and strips
 * remote HTML changelog markup into safe plaintext before UI presentation.
 */

#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QMetaType>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

class QNetworkReply;

/**
 * @brief Sanitized, HTTPS-validated release update metadata emitted by `AutoUpdater`.
 */
struct UpdateInfo {
    QString version;
    QString plainTextChanges;
    QUrl httpsDownloadUrl;

    [[nodiscard]] bool operator==(const UpdateInfo& other) const noexcept = default;
};

Q_DECLARE_METATYPE(UpdateInfo)

/**
 * @brief Headless HTTPS release-manifest checker (`vchar64_net`).
 *
 * Enforces HTTPS-only request and download URLs (`QNetworkRequest::NoLessSafeRedirectPolicy`),
 * a 10-second transfer timeout, a 64 KiB streaming response cap, and HTML-to-plaintext
 * changelog sanitization with zero `Qt6::Widgets` or `QSettings` dependencies.
 */
class AutoUpdater : public QObject {
    Q_OBJECT

public:
    /** @brief Canonical HTTPS endpoint hosting the `stable_version:` / `stable_url:` / `changes:` manifest. */
    static constexpr const char* kDefaultManifestUrl = "https://ricardoquesada.github.io/vchar64/LATEST_VERSION.txt";
    /** @brief Hard streaming response cap (`64 KiB`) preventing memory exhaustion from rogue servers. */
    static constexpr qsizetype kMaxResponseBytes = 64 * 1024;
    /** @brief Default transfer timeout (`10,000` ms) applied to both `QNetworkRequest` and `_timeoutTimer`. */
    static constexpr int kDefaultTransferTimeoutMs = 10000;
    /** @brief Maximum HTTPS-to-HTTPS redirect hops (`3`) permitted per check. */
    static constexpr int kMaxRedirects = 3;

    static AutoUpdater& getInstance();

    explicit AutoUpdater(QObject* parent = nullptr);
    ~AutoUpdater() override;

    AutoUpdater(const AutoUpdater&) = delete;
    AutoUpdater& operator=(const AutoUpdater&) = delete;

    void setManifestUrl(const QUrl& url);
    [[nodiscard]] QUrl manifestUrl() const { return _url; }

    void setCurrentVersion(const QString& version);
    [[nodiscard]] QString currentVersion() const { return _currentVersion; }

    void setTransferTimeoutMs(int timeoutMs) noexcept;
    [[nodiscard]] int transferTimeoutMs() const noexcept { return _transferTimeoutMs; }

    [[nodiscard]] bool isInProgress() const noexcept { return _inProgress; }
    [[nodiscard]] qsizetype bufferedResponseBytes() const noexcept { return _rawResponse.size(); }

    /** @brief Starts an asynchronous HTTPS update check against `manifestUrl()`. */
    void checkUpdate();
    /** @brief Starts an asynchronous HTTPS update check against `requestedUrl` (rejects non-HTTPS immediately). */
    void checkUpdate(const QUrl& requestedUrl);
    /** @brief Aborts any in-flight `QNetworkReply` safely without triggering false error signals. */
    void cancelDownload();

    /** @brief Resets streaming accumulators for deterministic offline chunk-ingestion tests. */
    void beginStreamingCheck() noexcept;
    /** @brief Appends `chunk` while enforcing `kMaxResponseBytes`; aborts and returns `false` on overflow. */
    bool ingestResponseChunk(QByteArrayView chunk);
    /** @brief Parses a complete UTF-8 manifest, validates semantic version & HTTPS download URL, and emits results. */
    void processManifestPayload(const QByteArray& utf8Manifest);

    /** @brief Verifies `url.isValid()`, non-relative URI, `"https"` scheme, and non-empty host. */
    [[nodiscard]] static bool isStrictHttpsUrl(const QUrl& url) noexcept;
    /** @brief Converts untrusted HTML changelog markup into plain text so UI views never execute rich-text tags. */
    [[nodiscard]] static QString sanitizeChangelogHtmlToPlainText(const QString& rawChanges);

signals:
    void updateAvailable(const UpdateInfo& info);
    void upToDate();
    void checkFailed(const QString& reason);
    void updateCheckFinished();

private slots:
    void httpFinished();
    void httpReadyRead();
    void onTransferTimeout();

private:
    void startRequest(const QUrl& requestedUrl);
    void failCheck(const QString& reason);

    QUrl _url;
    QString _currentVersion;
    int _transferTimeoutMs = kDefaultTransferTimeoutMs;

    QNetworkAccessManager _qnam;
    QNetworkReply* _reply = nullptr;
    QTimer _timeoutTimer;

    QByteArray _rawResponse;
    bool _httpRequestAborted = false;
    bool _oversizeAborted = false;
    bool _timedOut = false;
    bool _inProgress = false;
};
