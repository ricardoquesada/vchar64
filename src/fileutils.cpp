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

// Boundary-safe home-path shortening, multi-dot extension replacement, and drag-and-drop
// file classification helpers.

#include "fileutils.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>

#include <QDir>
#include <QFileInfo>
#include <QLatin1Char>
#include <QLatin1StringView>

namespace {

struct SuffixActionEntry {
    std::string_view suffix;
    FileUtils::DropAction action;
};

// Keep in sync with the file filters of MainWindow::on_actionOpen_triggered(),
// ImportKoalaDialog::on_pushButtonBrowse_clicked(), and ImportVICEDialog::on_pushButtonBrowse_clicked().
constexpr std::array<SuffixActionEntry, 10> kActionBySuffix = { {
    { "vchar64proj", FileUtils::DropAction::Open },
    { "raw", FileUtils::DropAction::Open },
    { "bin", FileUtils::DropAction::Open },
    { "fnt", FileUtils::DropAction::Open },
    { "prg", FileUtils::DropAction::Open },
    { "64c", FileUtils::DropAction::Open },
    { "ctm", FileUtils::DropAction::Open },
    { "koa", FileUtils::DropAction::ImportKoala },
    { "kla", FileUtils::DropAction::ImportKoala },
    { "vsf", FileUtils::DropAction::ImportViceSnapshot },
} };

#ifdef Q_OS_UNIX
// Shortens `path` relative to `home` using a `~` prefix when `path` equals `home` or lies
// strictly inside `home/`. Returns an empty string when `path` is outside `home`, ensuring
// sibling prefixes like `/Users/alice_smith/foo` are not rewritten for `/Users/alice`.
[[nodiscard]] QString TryShortenAgainstHome(const QString& path, const QString& home)
{
    if (path.isEmpty() || home.isEmpty() || home == QStringLiteral("/")) {
        return QString();
    }
    if (path == home) {
        return QStringLiteral("~");
    }
    if (path.startsWith(home + QLatin1Char('/'))) {
        return QStringLiteral("~") + QDir::toNativeSeparators(path.mid(home.size()));
    }
    return QString();
}
#endif

// Looks up the DropAction associated with a case-insensitive file `suffix`, or returns
// std::nullopt when the extension is unsupported.
[[nodiscard]] std::optional<FileUtils::DropAction> FindDropActionForSuffix(const QString& suffix)
{
    const QString lowercase_suffix = suffix.toLower();
    for (const SuffixActionEntry& entry : kActionBySuffix) {
        const QLatin1StringView entry_suffix(
            entry.suffix.data(),
            static_cast<qsizetype>(entry.suffix.size()));
        if (lowercase_suffix == entry_suffix) {
            return entry.action;
        }
    }
    return std::nullopt;
}

} // namespace

QString FileUtils::getShortNativePath(const QString& filename)
{
    if (filename.isEmpty()) {
        return QString();
    }

#ifdef Q_OS_UNIX
    const QString clean_input = QDir::cleanPath(filename);
    const QString clean_home = QDir::cleanPath(QDir::homePath());
    if (const QString shortened_path = TryShortenAgainstHome(clean_input, clean_home); !shortened_path.isEmpty()) {
        return shortened_path;
    }

    // Fall back to canonical path resolution when symlinks differ between input and HOME
    // (for example, macOS /var/folders/... vs /private/var/folders/...).
    const QString canonical_input = QFileInfo(filename).canonicalFilePath();
    if (canonical_input.isEmpty()) {
        return QDir::toNativeSeparators(clean_input);
    }

    const QString clean_canonical_input = QDir::cleanPath(canonical_input);
    if (const QString shortened_path = TryShortenAgainstHome(clean_canonical_input, clean_home);
        !shortened_path.isEmpty()) {
        return shortened_path;
    }

    const QString canonical_home = QFileInfo(QDir::homePath()).canonicalFilePath();
    if (!canonical_home.isEmpty()) {
        const QString clean_canonical_home = QDir::cleanPath(canonical_home);
        if (const QString shortened_path = TryShortenAgainstHome(clean_canonical_input, clean_canonical_home);
            !shortened_path.isEmpty()) {
            return shortened_path;
        }
    }
    return QDir::toNativeSeparators(clean_canonical_input);
#else
    return QDir::toNativeSeparators(QDir::cleanPath(filename));
#endif
}

QString FileUtils::replaceExtension(const QString& filename, const QString& new_extension)
{
    if (filename.isEmpty()) {
        return QString();
    }

    QString clean_extension = new_extension;
    while (clean_extension.startsWith(QLatin1Char('.'))) {
        clean_extension.remove(0, 1);
    }

    const QFileInfo file_info(filename);
    const QString base_name = file_info.completeBaseName();
    if (base_name.isEmpty()) {
        return filename;
    }

    const QString new_filename = clean_extension.isEmpty()
        ? base_name
        : (base_name + QLatin1Char('.') + clean_extension);

    // Preserve bare filenames (e.g. "untitled" -> "untitled.bin") without prepending "./".
    if (!filename.contains(QLatin1Char('/')) && !filename.contains(QLatin1Char('\\'))) {
        return new_filename;
    }

    return QDir(file_info.path()).filePath(new_filename);
}

QList<FileUtils::DroppedFile> FileUtils::classifyDroppedFiles(const QList<QUrl>& urls)
{
    QList<DroppedFile> files;
    for (const QUrl& url : urls) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QFileInfo file_info(url.toLocalFile());
        if (!file_info.isFile()) {
            continue;
        }
        const std::optional<DropAction> drop_action = FindDropActionForSuffix(file_info.suffix());
        if (!drop_action.has_value()) {
            continue;
        }
        const QString file_path = file_info.absoluteFilePath();
        const bool is_duplicate = std::any_of(
            files.cbegin(),
            files.cend(),
            [&file_path](const DroppedFile& dropped_file) {
                return dropped_file.path == file_path;
            });
        if (!is_duplicate) {
            files.append(DroppedFile { file_path, *drop_action });
        }
    }

    // Non-interactive opens first, then modal import dialogs; drop order is kept within each group.
    std::stable_partition(files.begin(), files.end(), [](const DroppedFile& dropped_file) {
        return dropped_file.action == DropAction::Open;
    });
    return files;
}
