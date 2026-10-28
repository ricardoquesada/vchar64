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

#include "fileutils.h"

/**
 * @file fileutils.cpp
 * @brief Implementation of boundary-safe home-path shortening and multi-dot extension replacement.
 */

#include <algorithm>

#include <QDir>
#include <QFileInfo>
#include <QHash>

QString FileUtils::getShortNativePath(const QString& filename)
{
    if (filename.isEmpty()) {
        return QString();
    }

#ifdef Q_OS_UNIX
    // Require either an exact match (`path == home`) or an explicit `/` separator immediately
    // after `home` so `/Users/alice_smith/foo` is never mangled into `~_smith/foo`.
    const auto tryShortenAgainstHome = [](const QString& path, const QString& home) -> QString {
        if (path.isEmpty() || home.isEmpty() || home == QStringLiteral("/")) {
            return { };
        }
        if (path == home) {
            return QStringLiteral("~");
        }
        if (path.startsWith(home + QLatin1Char('/'))) {
            return QStringLiteral("~") + QDir::toNativeSeparators(path.mid(home.size()));
        }
        return { };
    };

    const QString cleanInput = QDir::cleanPath(filename);
    const QString cleanHome = QDir::cleanPath(QDir::homePath());
    if (const QString shortened = tryShortenAgainstHome(cleanInput, cleanHome); !shortened.isEmpty()) {
        return shortened;
    }

    // Fall back to canonical path resolution when symlinks differ between input and HOME
    // (for example, macOS `/var/folders/...` vs `/private/var/folders/...`).
    const QString canonicalInput = QFileInfo(filename).canonicalFilePath();
    if (!canonicalInput.isEmpty()) {
        const QString cleanCanonicalInput = QDir::cleanPath(canonicalInput);
        if (const QString shortened = tryShortenAgainstHome(cleanCanonicalInput, cleanHome); !shortened.isEmpty()) {
            return shortened;
        }
        const QString canonicalHome = QFileInfo(QDir::homePath()).canonicalFilePath();
        if (!canonicalHome.isEmpty()) {
            const QString cleanCanonicalHome = QDir::cleanPath(canonicalHome);
            if (const QString shortened = tryShortenAgainstHome(cleanCanonicalInput, cleanCanonicalHome);
                !shortened.isEmpty()) {
                return shortened;
            }
        }
        return QDir::toNativeSeparators(cleanCanonicalInput);
    }
    return QDir::toNativeSeparators(cleanInput);
#else
    return QDir::toNativeSeparators(QDir::cleanPath(filename));
#endif
}

QString FileUtils::replaceExtension(const QString& filename, const QString& newExtension)
{
    if (filename.isEmpty()) {
        return QString();
    }

    QString cleanExt = newExtension;
    while (cleanExt.startsWith(QLatin1Char('.'))) {
        cleanExt.remove(0, 1);
    }

    const QFileInfo info(filename);
    const QString baseName = info.completeBaseName();
    if (baseName.isEmpty()) {
        return filename;
    }

    const QString newName = cleanExt.isEmpty()
        ? baseName
        : (baseName + QLatin1Char('.') + cleanExt);

    // Preserve bare filenames (e.g. "untitled" -> "untitled.bin") without prepending "./".
    if (!filename.contains(QLatin1Char('/')) && !filename.contains(QLatin1Char('\\'))) {
        return newName;
    }

    return QDir(info.path()).filePath(newName);
}

QList<FileUtils::DroppedFile> FileUtils::classifyDroppedFiles(const QList<QUrl>& urls)
{
    // Keep in sync with the file filters of MainWindow::on_actionOpen_triggered(),
    // ImportKoalaDialog::on_pushButtonBrowse_clicked(), and ImportVICEDialog::on_pushButtonBrowse_clicked().
    static const QHash<QString, DropAction> kActionBySuffix = {
        { QStringLiteral("vchar64proj"), DropAction::Open },
        { QStringLiteral("raw"), DropAction::Open },
        { QStringLiteral("bin"), DropAction::Open },
        { QStringLiteral("fnt"), DropAction::Open },
        { QStringLiteral("prg"), DropAction::Open },
        { QStringLiteral("64c"), DropAction::Open },
        { QStringLiteral("ctm"), DropAction::Open },
        { QStringLiteral("koa"), DropAction::ImportKoala },
        { QStringLiteral("kla"), DropAction::ImportKoala },
        { QStringLiteral("vsf"), DropAction::ImportViceSnapshot },
    };

    QList<DroppedFile> files;
    for (const QUrl& url : urls) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QFileInfo info(url.toLocalFile());
        if (!info.isFile()) {
            continue;
        }
        const auto it = kActionBySuffix.constFind(info.suffix().toLower());
        if (it == kActionBySuffix.constEnd()) {
            continue;
        }
        const QString path = info.absoluteFilePath();
        const bool duplicate = std::any_of(files.cbegin(), files.cend(), [&path](const DroppedFile& f) {
            return f.path == path;
        });
        if (!duplicate) {
            files.append({ path, it.value() });
        }
    }

    // Non-interactive opens first, then modal import dialogs; drop order is kept within each group.
    std::stable_partition(files.begin(), files.end(), [](const DroppedFile& f) {
        return f.action == DropAction::Open;
    });
    return files;
}
