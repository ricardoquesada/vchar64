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
 * @file importkoaladialog.cpp
 * @brief Interactive C64 Koala Painter multicolor bitmap-to-charset conversion dialog.
 *
 * Implements heuristic color quantization (neighbor-mask propagation, VIC-II
 * luminance proximity, and palette-cycle proximity) to map a `160x200` Koala
 * bitmap onto at most 256 unique multicolor `4x8` glyphs with shared `$D021`/`$D022`/`$D023`
 * registers and 3-bit per-tile Color RAM (`+ 8` multicolor bit), then transfers
 * the synthesized `std::unique_ptr<State>` via `MainWindow::adoptDocument()`.
 */

#include "importkoaladialog.h"
#include "ui_importkoaladialog.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <string_view>

#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QMouseEvent>

#include "mainwindow.h"
#include "palette.h"
#include "preferences.h"
#include "selectcolordialog.h"
#include "state.h"

namespace {

constexpr std::string_view kHexDigits = "0123456789ABCDEF";

[[nodiscard]] uint8_t dehexify(char h) noexcept
{
    if (h >= '0' && h <= '9') {
        return static_cast<uint8_t>(h - '0');
    }
    if (h >= 'A' && h <= 'F') {
        return static_cast<uint8_t>(h - 'A' + 10);
    }
    if (h >= 'a' && h <= 'f') {
        return static_cast<uint8_t>(h - 'a' + 10);
    }
    return 0U;
}

[[nodiscard]] int getValueFromKey(int x, int y, const std::array<char, 32>& key) noexcept
{
    if (x < 0 || x >= 4 || y < 0 || y >= 8) {
        return -1;
    }
    return static_cast<int>(dehexify(key[static_cast<size_t>(y * 4 + x)]));
}

} // namespace

ImportKoalaDialog::ImportKoalaDialog(QWidget* parent)
    : QDialog(parent)
    , ui(std::make_unique<Ui::ImportKoalaDialog>())
{
    ui->setupUi(this);

    const QString lastDir = Preferences::getInstance().getLastUsedDirectory();
    ui->lineEdit->setText(lastDir);

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

void ImportKoalaDialog::on_pushButtonBrowse_clicked()
{
    QString filter = tr("Koala files");
    const QString fn = QFileDialog::getOpenFileName(
        this,
        tr("Select Koala File"),
        ui->lineEdit->text(),
        tr("All files (*);;Koala files (*.koa *.kla);;"),
        &filter);

    if (!fn.isEmpty() && fn != _filepath) {
        _filepath = fn;
        ui->lineEdit->setText(fn);
        ui->widgetCharset->clean();

        validateKoalaFile(fn);
        updateWidgets();
    }
}

void ImportKoalaDialog::on_lineEdit_editingFinished()
{
    const QString filepath = ui->lineEdit->text();
    validateKoalaFile(filepath);
    updateWidgets();
}

void ImportKoalaDialog::validateKoalaFile(const QString& filepath)
{
    const QFileInfo info(filepath);

    if (info.exists() && info.isFile() && (info.size() == 10003 || info.size() == 10002)) {
        if (ui->widgetKoala->loadKoala(filepath)) {
            _validKoalaFile = convert();
            _koaLoaded = true;
        } else {
            _koaLoaded = false;
            _validKoalaFile = false;
        }
    } else {
        _koaLoaded = false;
        _validKoalaFile = false;
    }
}

int ImportKoalaDialog::findColorRAM(const std::vector<std::pair<int, int>>& usedColors, int& outHiColor)
{
    int cacheColor = -1;
    for (size_t i = 0; i < 16U && i < usedColors.size(); ++i) {
        const int color = usedColors[i].second;
        if (usedColors[i].first > 0
            && color != ui->widgetKoala->_d02xColors[0]
            && color != ui->widgetKoala->_d02xColors[1]
            && color != ui->widgetKoala->_d02xColors[2]) {
            if (color < 8) {
                return color;
            }

            if (ui->radioForegroundMostUsed->isChecked() || cacheColor == -1) {
                std::vector<int> colorsToFind;
                for (int j = 0; j < 8; ++j) {
                    if (j != ui->widgetKoala->_d02xColors[0]
                        && j != ui->widgetKoala->_d02xColors[1]
                        && j != ui->widgetKoala->_d02xColors[2]) {
                        colorsToFind.push_back(j);
                    }
                }

                if (ui->radioButtonLuminance->isChecked()) {
                    cacheColor = getColorByLuminanceProximity(color, colorsToFind);
                } else {
                    cacheColor = getColorByPaletteProximity(color, colorsToFind);
                }

                if (ui->radioForegroundMostUsed->isChecked()) {
                    outHiColor = cacheColor;
                    return outHiColor;
                }
            }
        }
    }

    if (cacheColor != -1) {
        outHiColor = cacheColor;
        return cacheColor;
    }
    return -1;
}

bool ImportKoalaDialog::tryChangeKey(int x, int y, std::array<char, 32>& key, quint8 mask, int hiColorRAM)
{
    static constexpr std::array<std::array<int, 2>, 8> kNeighborOffsets = { {
        { -1, 1 },
        { 0, 1 },
        { 1, 1 },
        { -1, 0 },
        { 1, 0 },
        { -1, -1 },
        { 0, -1 },
        { 1, -1 },
    } };

    const int colorIndex = getValueFromKey(x, y, key);
    if (colorIndex != _colorRAM
        && colorIndex != ui->widgetKoala->_d02xColors[0]
        && colorIndex != ui->widgetKoala->_d02xColors[1]
        && colorIndex != ui->widgetKoala->_d02xColors[2]) {
        if (hiColorRAM != -1 && colorIndex == hiColorRAM && _colorRAM >= 0 && _colorRAM < 16) {
            key[static_cast<size_t>(y * 4 + x)] = kHexDigits[static_cast<size_t>(_colorRAM)];
            return true;
        }

        std::vector<std::pair<int, int>> usedColors = {
            { 0, 0 }, { 0, 1 }, { 0, 2 }, { 0, 3 }, { 0, 4 }, { 0, 5 }, { 0, 6 }, { 0, 7 },
            { 0, 8 }, { 0, 9 }, { 0, 10 }, { 0, 11 }, { 0, 12 }, { 0, 13 }, { 0, 14 }, { 0, 15 }
        };

        for (size_t i = 0; i < kNeighborOffsets.size(); ++i) {
            if ((mask & static_cast<uint8_t>(1U << i)) != 0U) {
                const int xdiff = kNeighborOffsets[i][0];
                const int ydiff = kNeighborOffsets[i][1];

                const int neighborColor = getValueFromKey(x + xdiff, y + ydiff, key);
                if (neighborColor >= 0 && neighborColor < 16) {
                    usedColors[static_cast<size_t>(neighborColor)].first++;
                }
            }
        }

        std::sort(std::begin(usedColors), std::end(usedColors));
        const int neighColor = usedColors[15].second;
        if (neighColor >= 0 && neighColor < 16
            && (neighColor == _colorRAM
                || neighColor == ui->widgetKoala->_d02xColors[0]
                || neighColor == ui->widgetKoala->_d02xColors[1]
                || neighColor == ui->widgetKoala->_d02xColors[2])) {
            key[static_cast<size_t>(y * 4 + x)] = kHexDigits[static_cast<size_t>(neighColor)];
            return true;
        }
    }
    return false;
}

void ImportKoalaDialog::normalizeWithNeighborStrategy(std::array<char, 32>& key, int hiColorRAM)
{
    static constexpr std::array<quint8, 11> kMasks = {
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

    for (const quint8 mask : kMasks) {
        bool keyChanged = false;
        do {
            keyChanged = false;
            for (int y = 0; y < 8; ++y) {
                for (int x = 0; x < 4; ++x) {
                    keyChanged = tryChangeKey(x, y, key, mask, hiColorRAM) || keyChanged;
                }
            }
        } while (keyChanged);
    }
}

int ImportKoalaDialog::getColorByLuminanceProximity(int colorIndex, const std::vector<int>& colorsToFind)
{
    Q_ASSERT(colorIndex >= 0 && colorIndex < 16 && "Invalid Color Index");

    static constexpr std::array<int, 16> kLuminances = {
        0x01, 0x0d, 0x07, 0x03, 0x0f, 0x05, 0x0a, 0x0e,
        0x0c, 0x08, 0x04, 0x02, 0x0b, 0x09, 0x06, 0x00
    };

    std::vector<std::pair<int, int>> usedColors = {
        { 0, 0 }, { 0, 1 }, { 0, 2 }, { 0, 3 }, { 0, 4 }, { 0, 5 }, { 0, 6 }, { 0, 7 },
        { 0, 8 }, { 0, 9 }, { 0, 10 }, { 0, 11 }, { 0, 12 }, { 0, 13 }, { 0, 14 }, { 0, 15 }
    };

    const auto foundIter = std::find(std::begin(kLuminances), std::end(kLuminances), colorIndex);
    Q_ASSERT(foundIter != std::end(kLuminances) && "Invalid index color");
    const int idx = static_cast<int>(std::distance(std::begin(kLuminances), foundIter));

    for (int j = 0; j < 16; ++j) {
        const int tmpColor = kLuminances[static_cast<size_t>(j)];
        if (std::find(std::begin(colorsToFind), std::end(colorsToFind), tmpColor) != std::end(colorsToFind)) {
            usedColors[static_cast<size_t>(tmpColor)].first += std::max(0, 16 - std::abs(idx - j));
        }
    }

    std::sort(std::begin(usedColors), std::end(usedColors));
    return usedColors[15].second;
}

int ImportKoalaDialog::getColorByPaletteProximity(int colorIndex, const std::vector<int>& colorsToFind)
{
    static constexpr std::array<int, 9> kCycle1 = { 0, 6, 0xb, 4, 0xe, 5, 3, 0xd, 1 };
    static constexpr std::array<int, 8> kCycle2 = { 0, 9, 2, 8, 0xc, 0xa, 0xf, 1 };
    static constexpr std::array<int, 5> kCycle3 = { 0, 6, 0xc, 0xf, 1 };
    static constexpr std::array<int, 7> kCycle4 = { 9, 6, 8, 0xc, 0xa, 0xf, 0xd };
    static constexpr std::array<int, 9> kCycle5 = { 0, 6, 2, 4, 0xe, 5, 3, 7, 1 };

    std::vector<std::pair<int, int>> usedColors = {
        { 0, 0 }, { 0, 1 }, { 0, 2 }, { 0, 3 }, { 0, 4 }, { 0, 5 }, { 0, 6 }, { 0, 7 },
        { 0, 8 }, { 0, 9 }, { 0, 10 }, { 0, 11 }, { 0, 12 }, { 0, 13 }, { 0, 14 }, { 0, 15 }
    };

    struct Cycle {
        const int* array;
        int arrayLength;
    };
    const std::array<Cycle, 5> cycles = { {
        { kCycle1.data(), static_cast<int>(kCycle1.size()) },
        { kCycle2.data(), static_cast<int>(kCycle2.size()) },
        { kCycle3.data(), static_cast<int>(kCycle3.size()) },
        { kCycle4.data(), static_cast<int>(kCycle4.size()) },
        { kCycle5.data(), static_cast<int>(kCycle5.size()) },
    } };

    for (const auto& cycle : cycles) {
        int idx = -1;
        for (int j = 0; j < cycle.arrayLength; ++j) {
            if (cycle.array[j] == colorIndex) {
                idx = j;
                break;
            }
        }
        if (idx != -1) {
            for (int j = 0; j < cycle.arrayLength; ++j) {
                const int tmpColor = cycle.array[j];
                if (std::find(std::begin(colorsToFind), std::end(colorsToFind), tmpColor) != std::end(colorsToFind)) {
                    usedColors[static_cast<size_t>(tmpColor)].first += std::max(0, 9 - std::abs(idx - j));
                }
            }
        }
    }

    std::sort(std::begin(usedColors), std::end(usedColors));
    return usedColors[15].second;
}

void ImportKoalaDialog::normalizeWithColorStrategy(std::array<char, 32>& key, int hiColorRAM)
{
    Q_UNUSED(hiColorRAM)

    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 4; ++x) {
            const int colorIndex = getValueFromKey(x, y, key);

            if (colorIndex != _colorRAM
                && colorIndex != ui->widgetKoala->_d02xColors[0]
                && colorIndex != ui->widgetKoala->_d02xColors[1]
                && colorIndex != ui->widgetKoala->_d02xColors[2]) {
                const std::vector<int> colorsToFind = {
                    static_cast<int>(ui->widgetKoala->_d02xColors[0]),
                    static_cast<int>(ui->widgetKoala->_d02xColors[1]),
                    static_cast<int>(ui->widgetKoala->_d02xColors[2]),
                    _colorRAM,
                };

                const int newColor = ui->radioButtonLuminance->isChecked()
                    ? getColorByLuminanceProximity(colorIndex, colorsToFind)
                    : getColorByPaletteProximity(colorIndex, colorsToFind);
                if (newColor >= 0 && newColor < 16) {
                    key[static_cast<size_t>(y * 4 + x)] = kHexDigits[static_cast<size_t>(newColor)];
                }
            }
        }
    }
}

void ImportKoalaDialog::normalizeKey(std::array<char, 32>& key, int hiColorRAM)
{
    if (ui->radioButtonNeighbor->isChecked()) {
        normalizeWithNeighborStrategy(key, hiColorRAM);
    } else {
        normalizeWithColorStrategy(key, hiColorRAM);
    }
}

bool ImportKoalaDialog::processChardef(const std::string& key, std::array<quint8, 8>& outKey, quint8& outColorRAM)
{
    Q_ASSERT(key.size() == 8 * 4 && "Invalid Key Size");

    std::vector<std::pair<int, int>> usedColors = {
        { 0, 0 }, { 0, 1 }, { 0, 2 }, { 0, 3 }, { 0, 4 }, { 0, 5 }, { 0, 6 }, { 0, 7 },
        { 0, 8 }, { 0, 9 }, { 0, 10 }, { 0, 11 }, { 0, 12 }, { 0, 13 }, { 0, 14 }, { 0, 15 }
    };

    std::vector<std::pair<int, int>> invalidCoords;

    for (int y = 0; y < 8; ++y) {
        quint8 bits = 0U;
        for (int x = 0; x < 4; ++x) {
            const int colorIndex = static_cast<int>(dehexify(key[static_cast<size_t>(y * 4 + x)]));
            const unsigned int shift = static_cast<unsigned int>(6 - (x * 2));

            if (colorIndex == ui->widgetKoala->_d02xColors[0]) {
                // 0b00: background ($D021)
            } else if (colorIndex == ui->widgetKoala->_d02xColors[1]) {
                bits = static_cast<quint8>(bits | static_cast<quint8>(1U << shift));
            } else if (colorIndex == ui->widgetKoala->_d02xColors[2]) {
                bits = static_cast<quint8>(bits | static_cast<quint8>(2U << shift));
            } else {
                invalidCoords.emplace_back(x, y);
            }
            usedColors[static_cast<size_t>(colorIndex)].first++;
        }
        outKey[static_cast<size_t>(y)] = bits;
    }

    std::sort(std::begin(usedColors), std::end(usedColors));
    std::reverse(std::begin(usedColors), std::end(usedColors));

    int hiColorRAM = -1;
    _colorRAM = findColorRAM(usedColors, hiColorRAM);

    if (_colorRAM == -1) {
        Q_ASSERT(invalidCoords.empty() && "error in heuristic");
        _colorRAM = 0;
    }

    if (!invalidCoords.empty()) {
        std::array<char, 32> copyKey { };
        std::copy(key.begin(), key.end(), copyKey.begin());

        normalizeKey(copyKey, hiColorRAM);

        bool error = false;
        for (const auto& [x, y] : invalidCoords) {
            const int colorIndex = getValueFromKey(x, y, copyKey);
            const unsigned int shift = static_cast<unsigned int>(6 - (x * 2));
            const size_t rowIdx = static_cast<size_t>(y);

            if (colorIndex == ui->widgetKoala->_d02xColors[0]) {
                // 0b00
            } else if (colorIndex == ui->widgetKoala->_d02xColors[1]) {
                outKey[rowIdx] = static_cast<quint8>(outKey[rowIdx] | static_cast<quint8>(1U << shift));
            } else if (colorIndex == ui->widgetKoala->_d02xColors[2]) {
                outKey[rowIdx] = static_cast<quint8>(outKey[rowIdx] | static_cast<quint8>(2U << shift));
            } else if (colorIndex == _colorRAM) {
                outKey[rowIdx] = static_cast<quint8>(outKey[rowIdx] | static_cast<quint8>(3U << shift));
            } else {
                error = true;
            }
        }
        if (error) {
            auto deb = qDebug();
            deb << "Key with error"
                << "key:" << key.c_str()
                << "(" << ui->widgetKoala->_d02xColors[0] << ","
                << ui->widgetKoala->_d02xColors[1] << ","
                << ui->widgetKoala->_d02xColors[2] << ","
                << _colorRAM << ")"
                << "\n";

            for (const auto& pair : usedColors) {
                deb << pair.second << "=" << pair.first << ",";
            }
        }
    }

    outColorRAM = static_cast<quint8>(_colorRAM + 8);
    return true;
}

bool ImportKoalaDialog::convert()
{
    auto* bitmap = ui->widgetKoala;
    auto* charset = ui->widgetCharset;

    if (ui->radioD02xMostUsed->isChecked()) {
        bitmap->strategyD02xAny();
    } else if (ui->radioD02xMostUsedHi->isChecked()) {
        bitmap->strategyD02xAbove8();
    } else {
        const int d021 = std::clamp(ui->widgetD021->getColorIndex(), 0, 15);
        const int d022 = std::clamp(ui->widgetD022->getColorIndex(), 0, 15);
        const int d023 = std::clamp(ui->widgetD023->getColorIndex(), 0, 15);
        bitmap->_d02xColors[0] = static_cast<quint8>(d021);
        bitmap->_d02xColors[1] = static_cast<quint8>(d022);
        bitmap->_d02xColors[2] = static_cast<quint8>(d023);
    }

    QPalette basePalette;
    basePalette.setColor(QPalette::Base, Qt::white);
    basePalette.setColor(QPalette::Text, Qt::black);
    ui->lineEditUnique->setPalette(basePalette);

    for (size_t i = 0; i < 3U; ++i) {
        charset->_d02x[i] = bitmap->_d02xColors[i];
    }

    const std::array<ColorRectWidget*, 3> colorRects = { ui->widgetD021, ui->widgetD022, ui->widgetD023 };
    for (size_t i = 0; i < 3U; ++i) {
        if (bitmap->_d02xColors[i] != 255U) {
            colorRects[i]->setColorIndex(static_cast<int>(bitmap->_d02xColors[i]));
        }
    }

    _uniqueChars.clear();
    for (const auto& [cellKey, coords] : bitmap->_uniqueCells) {
        std::array<quint8, 8> chardef { };
        quint8 colorRAM = 0U;
        processChardef(cellKey, chardef, colorRAM);
        Q_ASSERT(colorRAM < 16U && "Invalid colorRAM");

        std::string charKey;
        charKey.reserve(17);
        for (const quint8 byteVal : chardef) {
            charKey += kHexDigits[static_cast<size_t>(byteVal >> 4U)];
            charKey += kHexDigits[static_cast<size_t>(byteVal & 0x0FU)];
        }
        charKey += kHexDigits[static_cast<size_t>(colorRAM)];

        auto existing = _uniqueChars.find(charKey);
        if (existing != _uniqueChars.end()) {
            existing->second.insert(existing->second.end(), coords.begin(), coords.end());
        } else {
            _uniqueChars.emplace(std::move(charKey), coords);
        }
    }

    const int uniqueChars = static_cast<int>(_uniqueChars.size());
    ui->lineEditUnique->setText(QString::number(uniqueChars));
    if (uniqueChars > 256) {
        QPalette errorPalette;
        errorPalette.setColor(QPalette::Base, Qt::red);
        errorPalette.setColor(QPalette::Text, Qt::white);
        ui->lineEditUnique->setPalette(errorPalette);

        ui->labelWarning->setStyleSheet(QStringLiteral("QLabel { color : red; }"));
        ui->labelWarning->setText(tr("Select an smaller region using mouse"));
        return false;
    }
    ui->labelWarning->setText(QString());

    int charsetCount = 0;
    for (const auto& [charKey, coords] : _uniqueChars) {
        std::array<quint8, 8> chardef { };
        for (size_t i = 0; i < 8U; ++i) {
            const quint8 hi = dehexify(charKey[i * 2U]);
            const quint8 lo = dehexify(charKey[i * 2U + 1U]);
            chardef[i] = static_cast<quint8>((hi << 4U) | lo);
        }
        const quint8 colorRAM = dehexify(charKey[16U]);

        charset->populateScreenAndColorRAM(coords, static_cast<quint8>(charsetCount), colorRAM);
        charset->setCharset(charsetCount, chardef.data());
        ++charsetCount;
    }

    charset->update();
    return true;
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

void ImportKoalaDialog::on_radioButtonNeighbor_toggled(bool checked)
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

void ImportKoalaDialog::onSelectedRegionUpdated(const QRect& region)
{
    Q_UNUSED(region)
    ui->widgetCharset->clean();
    ui->widgetKoala->parseKoala();
    _validKoalaFile = convert();
    updateWidgets();
}

void ImportKoalaDialog::on_checkBoxGrid_toggled(bool checked)
{
    ui->widgetCharset->enableGrid(checked);
    ui->widgetKoala->enableGrid(checked);
}

void ImportKoalaDialog::on_pushButtonImport_clicked()
{
    const QFileInfo info(ui->lineEdit->text());
    Preferences::getInstance().setLastUsedDirectory(info.absolutePath());

    auto state = std::make_unique<State>(info.filePath(), QSize(40, 25));

    state->setCharsetBytes(
        std::data(ui->widgetCharset->_charset),
        std::size(ui->widgetCharset->_charset));
    state->setTileColorsDirect(
        std::data(ui->widgetCharset->_colorRAMForChars),
        std::size(ui->widgetCharset->_colorRAMForChars));

    std::vector<uint8_t> mapCells(
        std::begin(ui->widgetCharset->_screenRAM),
        std::end(ui->widgetCharset->_screenRAM));
    (void)state->replaceMap(QSize(40, 25), std::move(mapCells));

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

void ImportKoalaDialog::mousePressEvent(QMouseEvent* event)
{
    const std::array<ColorRectWidget*, 3> widgets = {
        ui->widgetD021,
        ui->widgetD022,
        ui->widgetD023,
    };

    for (ColorRectWidget* widget : widgets) {
        const QPoint localPos = widget->mapFromParent(event->pos());
        if (widget->isEnabled() && widget->rect().contains(localPos)) {
            ui->radioD02xManual->setChecked(true);

            const int currentColor = widget->getColorIndex();
            SelectColorDialog diag(this);
            diag.setCurrentColor(currentColor);

            connect(&diag, &SelectColorDialog::colorSelected, this, [this, widget](int colorIndex) {
                widget->setColorIndex(colorIndex);
                convert();
            });

            if (diag.exec() == QDialog::Accepted) {
                widget->setColorIndex(diag.getSelectedColor());
                convert();
            } else {
                widget->setColorIndex(currentColor);
                convert();
            }
            event->accept();
            return;
        }
    }
    event->ignore();
}
