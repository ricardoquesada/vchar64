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
 * @file stateexport.h
 * @brief Headless, atomic (`QSaveFile`) asset exporters for `vchar64_core`.
 *
 * Implements native v3 `.vchar64proj` serialization alongside RAW binary,
 * C64 2-byte load-address `.prg`, 6502 `.byte` assembly, flat ISO C17
 * `const uint8_t[]` (`cc65` / `clang-mos` / `oscar64` compatible), and PNG
 * image writers.
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include <QImage>
#include <QString>
#include <QTextStream>

#include "core/result.h"

class State;

/**
 * @brief Headless, atomic (`QSaveFile`) exporters for `.vchar64proj`, RAW,
 *        C64 PRG, 6502 Assembly, ISO C17 `const uint8_t[]`, and PNG images.
 *
 * Resides in `vchar64_core` with zero `Qt6::Widgets` coupling. Every disk
 * writer validates inputs before touching the filesystem and commits through
 * `QSaveFile`, ensuring existing user files are never truncated on failure.
 */
class StateExport {
public:
    /**
     * @brief Serializes `state` into a v3 `.vchar64proj` file (`32`-byte little-endian header + charset + colors + map).
     *
     * Zero-initializes `VChar64Header` (including `reserved[2]`) so uninitialized stack
     * bytes never leak into saved project files.
     *
     * @return Total bytes written on success, or `ErrorCode` on validation/I/O failure.
     */
    [[nodiscard]] static vchar64::Result<qint64> saveVChar64(
        const State& state,
        const QString& filename);

    /** @brief Atomically writes `[buffer .. buffer + bufferSize)` as a headerless raw binary file. */
    [[nodiscard]] static vchar64::Result<qint64> saveRaw(
        const QString& filename,
        const uint8_t* buffer,
        size_t bufferSize);

    /** @brief Atomically writes a 2-byte little-endian `address` header followed by `bufferSize` payload bytes. */
    [[nodiscard]] static vchar64::Result<qint64> savePRG(
        const QString& filename,
        const uint8_t* buffer,
        size_t bufferSize,
        uint16_t address);

    /** @brief Atomically writes `buffer` as 16-byte-per-line `.byte $xx,...` 6502 assembly under sanitized `label`. */
    [[nodiscard]] static vchar64::Result<qint64> saveAsm(
        const QString& filename,
        const uint8_t* buffer,
        size_t bufferSize,
        const QString& label);

    /**
     * @brief Normalizes `rawName` into a valid ISO C17 / 6502 identifier (`[A-Za-z_][A-Za-z0-9_]*`).
     *
     * Replaces non-ASCII-alphanumeric characters with `_`, falls back to `"vchar64_data"`
     * when empty, and prepends `_` when the leading character is an ASCII digit.
     */
    [[nodiscard]] static QString sanitizeCIdentifier(const QString& rawName);

    /**
     * @brief Appends one flat `const uint8_t <id>[<N>] = { ... };` array definition to `out`.
     *
     * Emits explicit array bounds and 16 hex literals per row without GNU/Clang attribute
     * extensions so 6502 C toolchains (`cc65`, `oscar64`, `vbcc`, `clang-mos`) compile cleanly.
     */
    [[nodiscard]] static vchar64::Result<qint64> saveCSection(
        QTextStream& out,
        const uint8_t* buffer,
        size_t bufferSize,
        const QString& rawIdentifier);

    /** @brief Atomically writes `#include <stdint.h>` and a single `const uint8_t` array to `filename`. */
    [[nodiscard]] static vchar64::Result<qint64> saveC(
        const QString& filename,
        const uint8_t* buffer,
        size_t bufferSize,
        const QString& label);

    /** @brief Atomically encodes `image` as a PNG file via `QSaveFile` (rejects null `QImage` before opening disk). */
    [[nodiscard]] static vchar64::Result<qint64> savePNG(
        const QString& filename,
        const QImage& image);
};
