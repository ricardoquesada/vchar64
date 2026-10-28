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

/**
 * @file ilivepreviewsink.h
 * @brief Headless C64 live-preview transport interface and `40x25` Screen/Color RAM builders (`vchar64_net`).
 *
 * Isolates live-preview wire sinks (`ServerPreview` and future Vice binary-monitor
 * or Ultimate64 REST sinks) inside `vchar64_net` (`Qt6::Core` + `Qt6::Gui` + `Qt6::Network`,
 * zero `Qt6::Widgets`) and exposes pure `buildC64PreviewScreen()` / `buildC64PreviewColorRam()`
 * layout generators for deterministic headless unit testing.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <QObject>
#include <QString>

#include "serverprotocol.h"
#include "state.h"

namespace vchar64 {

inline constexpr int kC64ScreenCols = 40;
inline constexpr int kC64ScreenRows = 25;
inline constexpr size_t kC64ScreenBytes = 1000U;

inline constexpr int kC64CharsetCols = 32;
inline constexpr int kC64CharsetRows = 8;
inline constexpr size_t kC64CharsetRegionBytes = 320U; // 8 * 40 bytes

inline constexpr int kC64TileGridCols = 32;
inline constexpr int kC64TileStartRow = 12;
inline constexpr size_t kC64TileRegionOffset = 480U; // 12 * 40 bytes
inline constexpr size_t kC64TileRegionBytes = 520U; // 13 * 40 bytes ([480..1000))

using C64ScreenBuffer = std::array<uint8_t, kC64ScreenBytes>;

/**
 * @brief Builds a bounds-checked 1,000-byte (40 x 25) C64 Screen RAM buffer.
 *
 * Layout matches `server/vchar64d.c`:
 *
 * - Initialized entirely to `0x20` (PETSCII/screen space).
 * - Rows `0..7`, columns `0..31`: raw charset indices `0..255` (`row * 32 + col`).
 * - Rows `8..11`: blank separator (`0x20`).
 * - Rows `12..24`: tileset preview wrapped at 32 columns (`col + tw > 32 -> col = 0, row += th`)
 *   and guarded by whole-tile fit bounds (`col + tw <= 40` and `row + th <= 25`). Any tile whose
 *   bottom edge would exceed row 24 stops placement cleanly without partial tiles or out-of-bounds
 *   writes (eliminating the legacy 400-byte overflow on `7x4` and `7x7` tiles).
 */
[[nodiscard]] C64ScreenBuffer buildC64PreviewScreen(const State::TileProperties& props) noexcept;

/**
 * @brief Convenience overload extracting `TileProperties` from `state`.
 */
[[nodiscard]] C64ScreenBuffer buildC64PreviewScreen(const State& state) noexcept;

/**
 * @brief Builds a bounds-checked 1,000-byte (40 x 25) C64 `$D800` Color RAM buffer (`0x00..0x0F`).
 *
 * - In `State::FOREGROUND_COLOR_GLOBAL` mode, fills all 1,000 nibbles with the masked global
 *   foreground pen color (`& 0x0FU`).
 * - In `State::FOREGROUND_COLOR_PER_TILE` mode, initializes background cells to `0x08`, assigns
 *   each glyph cell in rows `0..7` (`cols 0..31`) its owning tile's 4-bit color, and assigns each
 *   whole tile placed in rows `12..24` its per-tile 4-bit color (`& 0x0FU`).
 */
[[nodiscard]] C64ScreenBuffer buildC64PreviewColorRam(const State& state) noexcept;

} // namespace vchar64

using vchar64::buildC64PreviewColorRam;
using vchar64::buildC64PreviewScreen;

/**
 * @brief Headless live-preview hardware/emulator sink interface (`vchar64_net`).
 *
 * Decouples C64 live-preview transports from `Qt6::Widgets` and `MainWindow`.
 */
class ILivePreviewSink : public QObject {
    Q_OBJECT

public:
    explicit ILivePreviewSink(QObject* parent = nullptr);
    ~ILivePreviewSink() override;

    /** @brief Binds the sink to `state` (or `nullptr` when all MDI documents close) and syncs remote RAM if connected. */
    virtual void setActiveState(const State* state) = 0;
    /** @brief Returns the currently observed `State` document, or `nullptr` if none is active. */
    [[nodiscard]] virtual const State* activeState() const noexcept = 0;

    /** @brief Initiates an asynchronous connection to a remote C64 preview endpoint at `host:port`. */
    virtual void connectPreview(
        const QString& host,
        quint16 port = static_cast<quint16>(VCHAR64_SERVER_LISTEN_PORT))
        = 0;
    /** @brief Gracefully closes the active preview session and flushes or clears queued frames. */
    virtual void disconnectPreview() = 0;
    /** @brief Returns `true` when the underlying transport handshake has completed and is ready for updates. */
    [[nodiscard]] virtual bool isConnected() const = 0;

signals:
    void previewConnected();
    void previewDisconnected();
    void previewError(const QString& message);
};
