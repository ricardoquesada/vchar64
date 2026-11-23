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

// Interactive C64 Koala Painter multicolor bitmap-to-charset conversion dialog.
//
// Implements heuristic color quantization (neighbor-mask propagation, VIC-II
// luminance proximity, and palette-cycle proximity) to map a 160x200 Koala
// bitmap onto at most 256 unique multicolor 4x8 glyphs with shared
// $D021/$D022/$D023 registers and 3-bit per-tile Color RAM (+ 8 multicolor
// bit), then transfers the synthesized std::unique_ptr<State> via
// MainWindow::adoptDocument().

#include "importkoaladialog.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QMouseEvent>
#include <QPalette>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>

#include "colorrectwidget.h"
#include "importkoalabitmapwidget.h"
#include "importkoalacharsetwidget.h"
#include "mainwindow.h"
#include "preferences.h"
#include "selectcolordialog.h"
#include "state.h"
#include "ui_importkoaladialog.h"

namespace {

constexpr int kColorCount = 16;
constexpr int kScreenColumns = 40;
constexpr int kScreenRows = 25;
constexpr qint64 kKoalaPrgFileSize = 10003;
constexpr qint64 kKoalaRawFileSize = 10002;
constexpr std::string_view kHexDigits = "0123456789ABCDEF";

constexpr std::array<std::array<int, 2>, 8> kNeighborOffsets = { {
    { -1, 1 },
    { 0, 1 },
    { 1, 1 },
    { -1, 0 },
    { 1, 0 },
    { -1, -1 },
    { 0, -1 },
    { 1, -1 },
} };

constexpr std::array<quint8, 11> kNeighborMasks = {
    0xffU,
    0x5aU,
    0xa5U,
    0x18U,
    0x42U,
    0x81U,
    0x24U,
    0x40U,
    0x10U,
    0x08U,
    0x02U,
};

constexpr std::array<int, kColorCount> kLuminanceOrder = {
    0x01,
    0x0d,
    0x07,
    0x03,
    0x0f,
    0x05,
    0x0a,
    0x0e,
    0x0c,
    0x08,
    0x04,
    0x02,
    0x0b,
    0x09,
    0x06,
    0x00,
};

constexpr std::array<int, 9> kCycle1 = { 0, 6, 0xb, 4, 0xe, 5, 3, 0xd, 1 };
constexpr std::array<int, 8> kCycle2 = { 0, 9, 2, 8, 0xc, 0xa, 0xf, 1 };
constexpr std::array<int, 5> kCycle3 = { 0, 6, 0xc, 0xf, 1 };
constexpr std::array<int, 7> kCycle4 = { 9, 6, 8, 0xc, 0xa, 0xf, 0xd };
constexpr std::array<int, 9> kCycle5 = { 0, 6, 2, 4, 0xe, 5, 3, 7, 1 };

struct PaletteCycle {
    const int* colors;
    int length;
};

constexpr std::array<PaletteCycle, 5> kPaletteCycles = { {
    { kCycle1.data(), static_cast<int>(kCycle1.size()) },
    { kCycle2.data(), static_cast<int>(kCycle2.size()) },
    { kCycle3.data(), static_cast<int>(kCycle3.size()) },
    { kCycle4.data(), static_cast<int>(kCycle4.size()) },
    { kCycle5.data(), static_cast<int>(kCycle5.size()) },
} };

// Decodes a single hexadecimal digit (`0`-`9`, `A`-`F`, `a`-`f`) to its
// numeric nibble value, returning 0 for any non-hex character.
[[nodiscard]] uint8_t Dehexify(char hex_char) noexcept
{
    if (hex_char >= '0' && hex_char <= '9') {
        return static_cast<uint8_t>(hex_char - '0');
    }
    if (hex_char >= 'A' && hex_char <= 'F') {
        return static_cast<uint8_t>(hex_char - 'A' + 10);
    }
    if (hex_char >= 'a' && hex_char <= 'f') {
        return static_cast<uint8_t>(hex_char - 'a' + 10);
    }
    return 0U;
}

// Returns the 4-bit C64 color index at pixel coordinate (`x`, `y`) inside a
// 4x8 cell hex signature, or -1 if the coordinates fall outside the cell.
[[nodiscard]] int GetValueFromKey(int x, int y, const std::array<char, 32>& key) noexcept
{
    if (x < 0 || x >= 4 || y < 0 || y >= 8) {
        return -1;
    }
    return static_cast<int>(Dehexify(key[static_cast<size_t>(y * 4 + x)]));
}

// Creates a 16-entry `(score, color_index)` histogram initialized to zero score
// for each C64 color index `0..15`.
[[nodiscard]] std::vector<std::pair<int, int>> CreateColorFrequencyVector()
{
    std::vector<std::pair<int, int>> frequencies;
    frequencies.reserve(kColorCount);
    for (int color_index = 0; color_index < kColorCount; ++color_index) {
        frequencies.emplace_back(0, color_index);
    }
    return frequencies;
}

[[nodiscard]] bool IsD02xColor(const std::array<quint8, 3>& d02x_colors, int color_index) noexcept
{
    return std::any_of(
        d02x_colors.begin(),
        d02x_colors.end(),
        [color_index](quint8 d02x_color) { return static_cast<int>(d02x_color) == color_index; });
}

} // namespace

ImportKoalaDialog::ImportKoalaDialog(QWidget* parent)
    : QDialog(parent)
    , ui(std::make_unique<Ui::ImportKoalaDialog>())
{
    ui->setupUi(this);

    const QString last_dir = Preferences::getInstance().getLastUsedDirectory();
    ui->lineEdit->setText(last_dir);

    connect(
        ui->widgetKoala,
        &ImportKoalaBitmapWidget::selectedRegionUpdated,
        this,
        &ImportKoalaDialog::onSelectedRegionUpdated);
    updateWidgets();
}

ImportKoalaDialog::~ImportKoalaDialog() = default;

const QString& ImportKoalaDialog::getFilepath() const
{
    return _filepath;
}

void ImportKoalaDialog::setFilepath(const QString& filepath)
{
    if (filepath.isEmpty() || filepath == _filepath) {
        return;
    }

    _filepath = filepath;
    ui->lineEdit->setText(filepath);
    ui->widgetCharset->clean();

    validateKoalaFile(filepath);
    updateWidgets();
}

void ImportKoalaDialog::mousePressEvent(QMouseEvent* event)
{
    const std::array<ColorRectWidget*, 3> color_widgets = {
        ui->widgetD021,
        ui->widgetD022,
        ui->widgetD023,
    };

    for (ColorRectWidget* widget : color_widgets) {
        const QPoint local_pos = widget->mapFromParent(event->pos());
        if (!widget->isEnabled() || !widget->rect().contains(local_pos)) {
            continue;
        }

        ui->radioD02xManual->setChecked(true);

        const int current_color = widget->getColorIndex();
        SelectColorDialog color_dialog(this);
        color_dialog.setCurrentColor(current_color);

        connect(&color_dialog, &SelectColorDialog::colorSelected, this, [this, widget](int color_index) {
            widget->setColorIndex(color_index);
            convert();
        });

        if (color_dialog.exec() == QDialog::Accepted) {
            widget->setColorIndex(color_dialog.getSelectedColor());
            convert();
        } else {
            widget->setColorIndex(current_color);
            convert();
        }
        event->accept();
        return;
    }

    event->ignore();
}

void ImportKoalaDialog::validateKoalaFile(const QString& filepath)
{
    const QFileInfo info(filepath);
    const bool valid_size = info.size() == kKoalaPrgFileSize || info.size() == kKoalaRawFileSize;
    if (!info.exists() || !info.isFile() || !valid_size) {
        _koaLoaded = false;
        _validKoalaFile = false;
        return;
    }

    if (!ui->widgetKoala->loadKoala(filepath)) {
        _koaLoaded = false;
        _validKoalaFile = false;
        return;
    }

    _validKoalaFile = convert();
    _koaLoaded = true;
}

bool ImportKoalaDialog::processChardef(
    const std::string& key,
    std::array<quint8, 8>& out_key,
    quint8& out_color_ram)
{
    Q_ASSERT(key.size() == 8 * 4 && "Invalid Key Size");

    std::vector<std::pair<int, int>> used_colors = CreateColorFrequencyVector();
    std::vector<std::pair<int, int>> invalid_coords;

    for (int y = 0; y < 8; ++y) {
        quint8 bits = 0U;
        for (int x = 0; x < 4; ++x) {
            const int color_index = static_cast<int>(Dehexify(key[static_cast<size_t>(y * 4 + x)]));
            const unsigned int shift = static_cast<unsigned int>(6 - (x * 2));

            if (color_index == ui->widgetKoala->_d02xColors[0]) {
                // 0b00: background ($D021)
            } else if (color_index == ui->widgetKoala->_d02xColors[1]) {
                bits = static_cast<quint8>(bits | static_cast<quint8>(1U << shift));
            } else if (color_index == ui->widgetKoala->_d02xColors[2]) {
                bits = static_cast<quint8>(bits | static_cast<quint8>(2U << shift));
            } else {
                invalid_coords.emplace_back(x, y);
            }
            used_colors[static_cast<size_t>(color_index)].first++;
        }
        out_key[static_cast<size_t>(y)] = bits;
    }

    std::sort(std::begin(used_colors), std::end(used_colors));
    std::reverse(std::begin(used_colors), std::end(used_colors));

    int hi_color_ram = -1;
    _colorRAM = findColorRAM(used_colors, hi_color_ram);

    if (_colorRAM == -1) {
        Q_ASSERT(invalid_coords.empty() && "error in heuristic");
        _colorRAM = 0;
    }

    if (!invalid_coords.empty()) {
        std::array<char, 32> copy_key { };
        std::copy(key.begin(), key.end(), copy_key.begin());

        normalizeKey(copy_key, hi_color_ram);

        bool error = false;
        for (const auto& [coord_x, coord_y] : invalid_coords) {
            const int color_index = GetValueFromKey(coord_x, coord_y, copy_key);
            const unsigned int shift = static_cast<unsigned int>(6 - (coord_x * 2));
            const size_t row_idx = static_cast<size_t>(coord_y);

            if (color_index == ui->widgetKoala->_d02xColors[0]) {
                // 0b00: background ($D021)
            } else if (color_index == ui->widgetKoala->_d02xColors[1]) {
                out_key[row_idx] = static_cast<quint8>(out_key[row_idx] | static_cast<quint8>(1U << shift));
            } else if (color_index == ui->widgetKoala->_d02xColors[2]) {
                out_key[row_idx] = static_cast<quint8>(out_key[row_idx] | static_cast<quint8>(2U << shift));
            } else if (color_index == _colorRAM) {
                out_key[row_idx] = static_cast<quint8>(out_key[row_idx] | static_cast<quint8>(3U << shift));
            } else {
                error = true;
            }
        }
        if (error) {
            QDebug debug_stream = qDebug();
            debug_stream << "Key with error"
                         << "key:" << key.c_str()
                         << "(" << ui->widgetKoala->_d02xColors[0] << ","
                         << ui->widgetKoala->_d02xColors[1] << ","
                         << ui->widgetKoala->_d02xColors[2] << ","
                         << _colorRAM << ")"
                         << "\n";

            for (const std::pair<int, int>& color_count : used_colors) {
                debug_stream << color_count.second << "=" << color_count.first << ",";
            }
        }
    }

    out_color_ram = static_cast<quint8>(_colorRAM + 8);
    return true;
}

int ImportKoalaDialog::findColorRAM(
    const std::vector<std::pair<int, int>>& used_colors,
    int& out_hi_color)
{
    int cache_color = -1;
    for (size_t i = 0; i < static_cast<size_t>(kColorCount) && i < used_colors.size(); ++i) {
        const int color = used_colors[i].second;
        if (used_colors[i].first <= 0 || IsD02xColor(ui->widgetKoala->_d02xColors, color)) {
            continue;
        }

        if (color < 8) {
            return color;
        }

        if (ui->radioForegroundMostUsed->isChecked() || cache_color == -1) {
            std::vector<int> colors_to_find;
            for (int j = 0; j < 8; ++j) {
                if (!IsD02xColor(ui->widgetKoala->_d02xColors, j)) {
                    colors_to_find.push_back(j);
                }
            }

            if (ui->radioButtonLuminance->isChecked()) {
                cache_color = getColorByLuminanceProximity(color, colors_to_find);
            } else {
                cache_color = getColorByPaletteProximity(color, colors_to_find);
            }

            if (ui->radioForegroundMostUsed->isChecked()) {
                out_hi_color = cache_color;
                return out_hi_color;
            }
        }
    }

    if (cache_color != -1) {
        out_hi_color = cache_color;
        return cache_color;
    }
    return -1;
}

void ImportKoalaDialog::normalizeKey(std::array<char, 32>& key, int hi_color_ram)
{
    if (ui->radioButtonNeighbor->isChecked()) {
        normalizeWithNeighborStrategy(key, hi_color_ram);
    } else {
        normalizeWithColorStrategy(key, hi_color_ram);
    }
}

void ImportKoalaDialog::normalizeWithColorStrategy(std::array<char, 32>& key, int hi_color_ram)
{
    Q_UNUSED(hi_color_ram)

    const std::array<quint8, 3>& d02x_colors = ui->widgetKoala->_d02xColors;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 4; ++x) {
            const int color_index = GetValueFromKey(x, y, key);
            if (color_index == _colorRAM || IsD02xColor(d02x_colors, color_index)) {
                continue;
            }

            const std::vector<int> colors_to_find = {
                static_cast<int>(d02x_colors[0]),
                static_cast<int>(d02x_colors[1]),
                static_cast<int>(d02x_colors[2]),
                _colorRAM,
            };

            const int new_color = ui->radioButtonLuminance->isChecked()
                ? getColorByLuminanceProximity(color_index, colors_to_find)
                : getColorByPaletteProximity(color_index, colors_to_find);
            if (new_color >= 0 && new_color < kColorCount) {
                key[static_cast<size_t>(y * 4 + x)] = kHexDigits[static_cast<size_t>(new_color)];
            }
        }
    }
}

void ImportKoalaDialog::normalizeWithNeighborStrategy(std::array<char, 32>& key, int hi_color_ram)
{
    for (const quint8 mask : kNeighborMasks) {
        bool key_changed = false;
        do {
            key_changed = false;
            for (int y = 0; y < 8; ++y) {
                for (int x = 0; x < 4; ++x) {
                    key_changed = tryChangeKey(x, y, key, mask, hi_color_ram) || key_changed;
                }
            }
        } while (key_changed);
    }
}

bool ImportKoalaDialog::tryChangeKey(
    int x,
    int y,
    std::array<char, 32>& key,
    quint8 mask,
    int hi_color_ram)
{
    const std::array<quint8, 3>& d02x_colors = ui->widgetKoala->_d02xColors;
    const int color_index = GetValueFromKey(x, y, key);
    if (color_index == _colorRAM || IsD02xColor(d02x_colors, color_index)) {
        return false;
    }

    if (hi_color_ram != -1 && color_index == hi_color_ram
        && _colorRAM >= 0 && _colorRAM < kColorCount) {
        key[static_cast<size_t>(y * 4 + x)] = kHexDigits[static_cast<size_t>(_colorRAM)];
        return true;
    }

    std::vector<std::pair<int, int>> used_colors = CreateColorFrequencyVector();

    for (size_t i = 0; i < kNeighborOffsets.size(); ++i) {
        if ((mask & static_cast<uint8_t>(1U << i)) == 0U) {
            continue;
        }
        const int dx = kNeighborOffsets[i][0];
        const int dy = kNeighborOffsets[i][1];

        const int neighbor_color = GetValueFromKey(x + dx, y + dy, key);
        if (neighbor_color >= 0 && neighbor_color < kColorCount) {
            used_colors[static_cast<size_t>(neighbor_color)].first++;
        }
    }

    std::sort(std::begin(used_colors), std::end(used_colors));
    const int best_neighbor_color = used_colors[15].second;
    if (best_neighbor_color >= 0 && best_neighbor_color < kColorCount
        && (best_neighbor_color == _colorRAM || IsD02xColor(d02x_colors, best_neighbor_color))) {
        key[static_cast<size_t>(y * 4 + x)] = kHexDigits[static_cast<size_t>(best_neighbor_color)];
        return true;
    }

    return false;
}

int ImportKoalaDialog::getColorByLuminanceProximity(
    int color_index,
    const std::vector<int>& colors_to_find)
{
    Q_ASSERT(color_index >= 0 && color_index < kColorCount && "Invalid Color Index");

    std::vector<std::pair<int, int>> used_colors = CreateColorFrequencyVector();

    const auto found_iter = std::find(
        std::begin(kLuminanceOrder),
        std::end(kLuminanceOrder),
        color_index);
    Q_ASSERT(found_iter != std::end(kLuminanceOrder) && "Invalid index color");
    const int target_idx = static_cast<int>(std::distance(std::begin(kLuminanceOrder), found_iter));

    for (int j = 0; j < kColorCount; ++j) {
        const int candidate_color = kLuminanceOrder[static_cast<size_t>(j)];
        if (std::find(std::begin(colors_to_find), std::end(colors_to_find), candidate_color)
            != std::end(colors_to_find)) {
            used_colors[static_cast<size_t>(candidate_color)].first
                += std::max(0, kColorCount - std::abs(target_idx - j));
        }
    }

    std::sort(std::begin(used_colors), std::end(used_colors));
    return used_colors[15].second;
}

int ImportKoalaDialog::getColorByPaletteProximity(
    int color_index,
    const std::vector<int>& colors_to_find)
{
    std::vector<std::pair<int, int>> used_colors = CreateColorFrequencyVector();

    for (const PaletteCycle& cycle : kPaletteCycles) {
        int match_idx = -1;
        for (int j = 0; j < cycle.length; ++j) {
            if (cycle.colors[j] == color_index) {
                match_idx = j;
                break;
            }
        }
        if (match_idx == -1) {
            continue;
        }

        for (int j = 0; j < cycle.length; ++j) {
            const int candidate_color = cycle.colors[j];
            if (std::find(std::begin(colors_to_find), std::end(colors_to_find), candidate_color)
                != std::end(colors_to_find)) {
                used_colors[static_cast<size_t>(candidate_color)].first
                    += std::max(0, 9 - std::abs(match_idx - j));
            }
        }
    }

    std::sort(std::begin(used_colors), std::end(used_colors));
    return used_colors[15].second;
}

bool ImportKoalaDialog::convert()
{
    ImportKoalaBitmapWidget* const bitmap_widget = ui->widgetKoala;
    ImportKoalaCharsetWidget* const charset_widget = ui->widgetCharset;

    if (ui->radioD02xMostUsed->isChecked()) {
        bitmap_widget->strategyD02xAny();
    } else if (ui->radioD02xMostUsedHi->isChecked()) {
        bitmap_widget->strategyD02xAbove8();
    } else {
        const int d021 = std::clamp(ui->widgetD021->getColorIndex(), 0, 15);
        const int d022 = std::clamp(ui->widgetD022->getColorIndex(), 0, 15);
        const int d023 = std::clamp(ui->widgetD023->getColorIndex(), 0, 15);
        bitmap_widget->_d02xColors[0] = static_cast<quint8>(d021);
        bitmap_widget->_d02xColors[1] = static_cast<quint8>(d022);
        bitmap_widget->_d02xColors[2] = static_cast<quint8>(d023);
    }

    QPalette base_palette;
    base_palette.setColor(QPalette::Base, Qt::white);
    base_palette.setColor(QPalette::Text, Qt::black);
    ui->lineEditUnique->setPalette(base_palette);

    for (size_t i = 0; i < 3U; ++i) {
        charset_widget->_d02x[i] = bitmap_widget->_d02xColors[i];
    }

    const std::array<ColorRectWidget*, 3> color_rects = {
        ui->widgetD021,
        ui->widgetD022,
        ui->widgetD023,
    };
    for (size_t i = 0; i < 3U; ++i) {
        if (bitmap_widget->_d02xColors[i] != 255U) {
            color_rects[i]->setColorIndex(static_cast<int>(bitmap_widget->_d02xColors[i]));
        }
    }

    _uniqueChars.clear();
    for (const auto& [cell_key, coords] : bitmap_widget->_uniqueCells) {
        std::array<quint8, 8> char_bytes { };
        quint8 color_ram = 0U;
        processChardef(cell_key, char_bytes, color_ram);
        Q_ASSERT(color_ram < 16U && "Invalid colorRAM");

        std::string char_key;
        char_key.reserve(17);
        for (const quint8 byte_value : char_bytes) {
            char_key += kHexDigits[static_cast<size_t>(byte_value >> 4U)];
            char_key += kHexDigits[static_cast<size_t>(byte_value & 0x0FU)];
        }
        char_key += kHexDigits[static_cast<size_t>(color_ram)];

        const auto existing = _uniqueChars.find(char_key);
        if (existing != _uniqueChars.end()) {
            existing->second.insert(existing->second.end(), coords.begin(), coords.end());
        } else {
            _uniqueChars.emplace(std::move(char_key), coords);
        }
    }

    const int unique_chars = static_cast<int>(_uniqueChars.size());
    ui->lineEditUnique->setText(QString::number(unique_chars));
    if (unique_chars > 256) {
        QPalette error_palette;
        error_palette.setColor(QPalette::Base, Qt::red);
        error_palette.setColor(QPalette::Text, Qt::white);
        ui->lineEditUnique->setPalette(error_palette);

        ui->labelWarning->setStyleSheet(QStringLiteral("QLabel { color : red; }"));
        ui->labelWarning->setText(tr("Select an smaller region using mouse"));
        return false;
    }
    ui->labelWarning->setText(QString());

    int charset_count = 0;
    for (const auto& [char_key, coords] : _uniqueChars) {
        std::array<quint8, 8> char_bytes { };
        for (size_t i = 0; i < 8U; ++i) {
            const quint8 hi = Dehexify(char_key[i * 2U]);
            const quint8 lo = Dehexify(char_key[i * 2U + 1U]);
            char_bytes[i] = static_cast<quint8>((hi << 4U) | lo);
        }
        const quint8 color_ram = Dehexify(char_key[16U]);

        charset_widget->populateScreenAndColorRAM(coords, static_cast<quint8>(charset_count), color_ram);
        charset_widget->setCharset(charset_count, char_bytes.data());
        ++charset_count;
    }

    charset_widget->update();
    return true;
}

void ImportKoalaDialog::updateWidgets()
{
    const std::array<QWidget*, 14> widgets = {
        ui->radioButtonLuminance,
        ui->radioButtonPalette,
        ui->radioButtonNeighbor,
        ui->radioForegroundMostUsed,
        ui->radioForegroundMostUsedLow,
        ui->radioD02xManual,
        ui->radioD02xMostUsed,
        ui->radioD02xMostUsedHi,
        ui->widgetCharset,
        ui->widgetKoala,
        ui->lineEditUnique,
        ui->widgetD021,
        ui->widgetD022,
        ui->widgetD023,
    };

    for (QWidget* widget : widgets) {
        widget->setEnabled(_koaLoaded);
    }

    ui->pushButtonImport->setEnabled(_validKoalaFile);
}

void ImportKoalaDialog::on_radioD02xManual_toggled(bool checked)
{
    if (!checked) {
        return;
    }
    convert();
}

void ImportKoalaDialog::on_radioD02xMostUsed_toggled(bool checked)
{
    if (!checked) {
        return;
    }
    convert();
}

void ImportKoalaDialog::on_radioD02xMostUsedHi_toggled(bool checked)
{
    if (!checked) {
        return;
    }
    convert();
}

void ImportKoalaDialog::on_radioForegroundMostUsed_toggled(bool checked)
{
    if (!checked) {
        return;
    }
    convert();
}

void ImportKoalaDialog::on_radioForegroundMostUsedLow_toggled(bool checked)
{
    if (!checked) {
        return;
    }
    convert();
}

void ImportKoalaDialog::on_radioButtonLuminance_toggled(bool checked)
{
    if (!checked) {
        return;
    }
    convert();
}

void ImportKoalaDialog::on_radioButtonPalette_toggled(bool checked)
{
    if (!checked) {
        return;
    }
    convert();
}

void ImportKoalaDialog::on_radioButtonNeighbor_toggled(bool checked)
{
    if (!checked) {
        return;
    }
    convert();
}

void ImportKoalaDialog::on_checkBoxGrid_toggled(bool checked)
{
    ui->widgetCharset->enableGrid(checked);
    ui->widgetKoala->enableGrid(checked);
}

void ImportKoalaDialog::on_pushButtonBrowse_clicked()
{
    QString filter = tr("Koala files");
    const QString selected_file = QFileDialog::getOpenFileName(
        this,
        tr("Select Koala File"),
        ui->lineEdit->text(),
        tr("All files (*);;Koala files (*.koa *.kla);;"),
        &filter);

    setFilepath(selected_file);
}

void ImportKoalaDialog::on_lineEdit_editingFinished()
{
    const QString filepath = ui->lineEdit->text();
    validateKoalaFile(filepath);
    updateWidgets();
}

void ImportKoalaDialog::on_pushButtonImport_clicked()
{
    const QFileInfo info(ui->lineEdit->text());
    Preferences::getInstance().setLastUsedDirectory(info.absolutePath());

    auto state = std::make_unique<State>(info.filePath(), QSize(kScreenColumns, kScreenRows));

    state->setCharsetBytes(
        std::data(ui->widgetCharset->_charset),
        std::size(ui->widgetCharset->_charset));
    state->setTileColorsDirect(
        std::data(ui->widgetCharset->_colorRAMForChars),
        std::size(ui->widgetCharset->_colorRAMForChars));

    std::vector<uint8_t> map_cells(
        std::begin(ui->widgetCharset->_screenRAM),
        std::end(ui->widgetCharset->_screenRAM));
    static_cast<void>(state->replaceMap(QSize(kScreenColumns, kScreenRows), std::move(map_cells)));

    state->setPenColorDirect(State::PEN_BACKGROUND, ui->widgetCharset->_d02x[0]);
    state->setPenColorDirect(State::PEN_MULTICOLOR1, ui->widgetCharset->_d02x[1]);
    state->setPenColorDirect(State::PEN_MULTICOLOR2, ui->widgetCharset->_d02x[2]);
    state->setPenColorDirect(State::PEN_FOREGROUND, ui->widgetCharset->_colorRAMForChars[0]);
    state->setMulticolorModeDirect(true);
    state->setForegroundColorModeDirect(State::FOREGROUND_COLOR_PER_TILE);

    MainWindow::getInstance()->adoptDocument(std::move(state));
    accept();
}

void ImportKoalaDialog::on_pushButtonCancel_clicked()
{
    reject();
}

void ImportKoalaDialog::onSelectedRegionUpdated(const QRect& region)
{
    Q_UNUSED(region)
    ui->widgetCharset->clean();
    ui->widgetKoala->parseKoala();
    _validKoalaFile = convert();
    updateWidgets();
}
