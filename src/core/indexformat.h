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
 * @file indexformat.h
 * @brief Decimal + `$`-hex formatting for C64 char, tile, and map indices shown in the UI.
 */

#include <QString>

namespace vchar64 {

/**
 * @brief Formats an index as decimal followed by a `$`-prefixed uppercase hex value.
 *
 * Hex digits are zero-padded to at least two digits and widen naturally for
 * values above `255`. Negative (invalid) values are rendered as plain decimal.
 *
 * Examples:
 * - `0`   -> `"0 ($00)"`
 * - `66`  -> `"66 ($42)"`
 * - `300` -> `"300 ($12C)"`
 * - `-1`  -> `"-1"`
 */
[[nodiscard]] inline QString formatIndexDecHex(int value)
{
    if (value < 0) {
        return QString::number(value);
    }
    const QString hex = QString::number(value, 16).toUpper().rightJustified(2, QLatin1Char('0'));
    return QStringLiteral("%1 ($%2)").arg(value).arg(hex);
}

} // namespace vchar64
