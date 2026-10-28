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

#pragma once

/**
 * @file fileutils.h
 * @brief Cross-platform path display shortening and multi-dot extension manipulation helpers.
 */

#include <QList>
#include <QString>
#include <QUrl>

/**
 * @brief Cross-platform path display and extension-manipulation utilities.
 */
class FileUtils {
public:
    /**
     * @brief Shortens paths inside the user's home directory (`~` or `~/...`) on Unix
     *        while enforcing directory-component boundaries so sibling prefixes
     *        (for example, `/Users/alice_smith` when `HOME=/Users/alice`) are never corrupted.
     * @param filename Input file or directory path.
     * @return Native-separator path shortened with `~` when inside `QDir::homePath()`.
     */
    [[nodiscard]] static QString getShortNativePath(const QString& filename);

    /**
     * @brief Replaces or appends an extension (`newExtension` with or without leading `.`)
     *        while preserving multi-dot stems (`QFileInfo::completeBaseName()`) and bare
     *        filenames (for example, `"untitled"` -> `"untitled.bin"` without prepending `"./"`).
     * @param filename Original path or bare filename.
     * @param newExtension Replacement extension (empty string strips the final extension).
     * @return Path with its final extension replaced or appended.
     */
    [[nodiscard]] static QString replaceExtension(
        const QString& filename,
        const QString& newExtension);

    /** @brief What `MainWindow` should do with a dropped file. */
    enum class DropAction {
        Open, ///< Load directly via `MainWindow::openFile()`.
        ImportKoala, ///< Open the Koala import dialog pre-loaded with the file.
        ImportViceSnapshot, ///< Open the VICE snapshot import dialog pre-loaded with the file.
    };

    /** @brief A dropped local file and the action chosen for it. */
    struct DroppedFile {
        QString path;
        DropAction action = DropAction::Open;

        bool operator==(const DroppedFile&) const = default;
    };

    /**
     * @brief Classifies dropped URLs by extension (case-insensitive) into open or import actions.
     *
     * - Open: `.vchar64proj`, `.raw`, `.bin`, `.fnt`, `.prg`, `.64c`, `.ctm` (the Open dialog's formats).
     * - ImportKoala: `.koa`, `.kla` (the Koala import dialog's formats).
     * - ImportViceSnapshot: `.vsf` (the VICE import dialog's format).
     *
     * Any other extension is rejected, because `State::openFile()` would silently load it as raw
     * bytes. Non-local URLs, directories, missing files, and duplicates are dropped too.
     *
     * The result is in processing order: all Open entries first, then imports, each group
     * in drop order. Opening is non-interactive, while each import shows a modal dialog.
     *
     * Example: `{pic.koa, https://x/b.prg, level.ctm, notes.txt}` ->
     * `{{level.ctm, Open}, {pic.koa, ImportKoala}}`.
     *
     * @param urls URLs from `QMimeData::urls()`.
     * @return Files to process with their actions; empty if nothing in the drop is supported.
     */
    [[nodiscard]] static QList<DroppedFile> classifyDroppedFiles(const QList<QUrl>& urls);
};
