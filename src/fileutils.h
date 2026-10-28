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

#include <QString>

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
};
