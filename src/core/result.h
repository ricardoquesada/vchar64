/****************************************************************************
Copyright 2026 Ricardo Quesada

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
 * @file result.h
 * @brief Zero-overhead, exception-free error propagation primitives for `vchar64_core` and `vchar64_net`.
 *
 * Replaces C++ exceptions (unsupported across Qt meta-object signal/slot boundaries)
 * and direct UI side effects (`MainWindow` status-bar messages or modal dialogs) inside
 * headless parsers, exporters, and clipboard codecs.
 */

#include <utility>
#include <variant>

#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace vchar64 {

/**
 * @brief Structured domain and I/O error codes returned by headless core operations.
 */
enum class ErrorCode {
    FileOpenFailed,
    FileReadFailed,
    FileWriteFailed,
    FileCommitFailed,
    FileTooSmall,
    InvalidMagic,
    UnsupportedVersion,
    HeaderOutOfBounds,
    UnexpandedCtmUnsupported,
    CorruptViceSnapshot,
    InvalidDimensions,
    InvalidExportFormat,
};

/**
 * @brief Pairs a machine-testable ErrorCode with a human-readable diagnostic message.
 */
struct IoError {
    ErrorCode code = ErrorCode::FileOpenFailed;
    QString detail;
};

/**
 * @brief Non-fatal diagnostics and byte statistics produced by binary importers.
 *
 * `warnings` collects user-visible truncation notices (for example, when a PC CharPad
 * `.ctm` file exceeds 256 C64 hardware glyphs or tiles and is clamped during import).
 */
struct ImportReport {
    qint64 bytesRead = 0;
    QStringList warnings;
};

/**
 * @brief Value-semantic, exception-free result wrapper holding either `T` or `IoError`.
 *
 * Marked `[[nodiscard]]` so callers cannot silently ignore I/O or validation failures.
 */
template <typename T>
class [[nodiscard]] Result {
public:
    /** @brief Constructs a successful result owning `value`. */
    static Result ok(T value)
    {
        return Result(std::move(value));
    }

    /** @brief Constructs a failed result with `code` and an optional diagnostic `detail`. */
    static Result err(ErrorCode code, QString detail = { })
    {
        return Result(IoError { code, std::move(detail) });
    }

    /** @brief Returns `true` when this result holds a valid `T` value. */
    [[nodiscard]] bool isOk() const noexcept
    {
        return std::holds_alternative<T>(_storage);
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return isOk();
    }

    /** @brief Accesses the contained value; requires `isOk() == true`. */
    [[nodiscard]] const T& value() const&
    {
        return std::get<T>(_storage);
    }

    /** @brief Accesses the mutable contained value; requires `isOk() == true`. */
    [[nodiscard]] T& value() &
    {
        return std::get<T>(_storage);
    }

    /** @brief Moves out the contained value; requires `isOk() == true`. */
    [[nodiscard]] T&& value() &&
    {
        return std::get<T>(std::move(_storage));
    }

    /** @brief Accesses the failure payload; requires `isOk() == false`. */
    [[nodiscard]] const IoError& error() const&
    {
        return std::get<IoError>(_storage);
    }

    /** @brief Returns `error().detail` on failure, or an empty `QString` on success. */
    [[nodiscard]] QString errorMessage() const
    {
        return isOk() ? QString { } : std::get<IoError>(_storage).detail;
    }

private:
    explicit Result(T val)
        : _storage(std::move(val))
    {
    }

    explicit Result(IoError ioError)
        : _storage(std::move(ioError))
    {
    }

    std::variant<T, IoError> _storage;
};

/**
 * @brief `void` specialization of `Result` for fallible operations with no return value.
 */
template <>
class [[nodiscard]] Result<void> {
public:
    static Result ok()
    {
        return Result(true);
    }

    static Result err(ErrorCode code, QString detail = { })
    {
        return Result(IoError { code, std::move(detail) });
    }

    [[nodiscard]] bool isOk() const noexcept
    {
        return std::holds_alternative<std::monostate>(_storage);
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return isOk();
    }

    [[nodiscard]] const IoError& error() const&
    {
        return std::get<IoError>(_storage);
    }

    [[nodiscard]] QString errorMessage() const
    {
        return isOk() ? QString { } : std::get<IoError>(_storage).detail;
    }

private:
    explicit Result(bool)
        : _storage(std::monostate { })
    {
    }

    explicit Result(IoError ioError)
        : _storage(std::move(ioError))
    {
    }

    std::variant<std::monostate, IoError> _storage;
};

} // namespace vchar64
