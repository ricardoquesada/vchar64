/****************************************************************************
Copyright 2016 Ricardo Quesada

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

#include "importkoaladialog.h"
#include "ui_importkoaladialog.h"

#include <random>

#include <QFileDialog>
#include <QSettings>
#include <QDebug>
#include <QMouseEvent>

#include "mainwindow.h"
#include "palette.h"
#include "selectcolordialog.h"

static const char* _hex ="0123456789ABCDEF";

ImportKoalaDialog::ImportKoalaDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ImportKoalaDialog)
    , _validKoalaFile(false)
{
    ui->setupUi(this);

    auto lastDir = QSettings("RetroMoe","VChar64").value("dir/lastdir").toString();
    ui->lineEdit->setText(lastDir);

    updateWidgets();
}

ImportKoalaDialog::~ImportKoalaDialog()
{
    delete ui;
}

void ImportKoalaDialog::on_pushButton_clicked()
{
    auto filter = tr("Koala files");
    auto fn = QFileDialog::getOpenFileName(this,
                                           tr("Select Koala File"),
                                           ui->lineEdit->text(),
                                           tr(
                                               "All files (*);;" \
                                               "Koala files (*.koa *.kla);;"
                                           ),
                                           &filter
                                           /*,QFileDialog::DontUseNativeDialog*/
                                           );

    if (fn.length()> 0)
    {
        if (fn != _filepath)
        {
            _filepath = fn;
            ui->lineEdit->setText(fn);

            _validKoalaFile = validateKoalaFile(fn);

            updateWidgets();
        }
    }
}

void ImportKoalaDialog::on_lineEdit_editingFinished()
{
    auto filepath = ui->lineEdit->text();
    _validKoalaFile = validateKoalaFile(filepath);
    updateWidgets();
}


// helpers


bool ImportKoalaDialog::validateKoalaFile(const QString& filepath)
{
    QFileInfo info(filepath);

    if (info.exists() && info.isFile() && (info.size() == 10003 || info.size() == 10002))
    {
        ui->widgetKoala->loadKoala(filepath);
        convert();
        return true;
    }
    return false;
}

int ImportKoalaDialog::findColorRAM(const std::vector<std::pair<int,int>>& usedColors, int* outHiColor)
{
    int cacheColor = -1;
    for (int i=0; i<16; i++)
    {
        auto color = usedColors[i].second;
        if (usedColors[i].first > 0 &&
                color != ui->widgetKoala->_d02xColors[0] &&
                color != ui->widgetKoala->_d02xColors[1] &&
                color != ui->widgetKoala->_d02xColors[2])
        {
            // valid both for "low" and "any"
            if (color < 8)
                return color;

            // only for "any"
            if (ui->radioForegroundMostUsed->isChecked() || cacheColor == -1)
            {
                std::vector<int> colorsToFind;
                for (int i=0; i<8; i++)
                {
                    if (i != ui->widgetKoala->_d02xColors[0] &&
                        i != ui->widgetKoala->_d02xColors[1] &&
                        i != ui->widgetKoala->_d02xColors[2])
                        colorsToFind.push_back(i);
                }

                cacheColor = getColorByPaletteProximity(color, colorsToFind);

                // inform which is the Hi Color used to create the Color Ram
                if (ui->radioForegroundMostUsed->isChecked())
                {
                    *outHiColor = cacheColor;
                    return *outHiColor;
                }
            }
        }
    }

    if (cacheColor != -1)
    {
        *outHiColor = cacheColor;
        return cacheColor;
    }
    return -1;
}


static int getValueFromKey(int x, int y, const char* key)
{
    if (x<0 || x>=4 || y<0 || y>=8)
        return -1;
    int c = key[y*4+x] - '0';
    if (c>9)
        c -= 7;
    return c;
}

bool ImportKoalaDialog::tryChangeKey(int x, int y, char* key, quint8 mask, int hiColorRAM)
{
    static const int masks[8][2] = {
        {-1, 1},   // top-left
        { 0, 1},   // top
        { 1, 1},   // top-right
        {-1, 0},   // left
        { 1, 0},   // right
        {-1,-1},   // bottom-left
        { 0,-1},   // bottom
        { 1,-1},   // bottom-right;
    };
    const int totalMasks = sizeof(masks) / sizeof(masks[0]);

    // find invalid colors
    int colorIndex = getValueFromKey(x, y, key);
    if (colorIndex != _colorRAM &&
            colorIndex != ui->widgetKoala->_d02xColors[0] &&
            colorIndex != ui->widgetKoala->_d02xColors[1] &&
            colorIndex != ui->widgetKoala->_d02xColors[2])
    {
        // both colorIndex and hiColorRAM could be -1 at the same time (valid scenario)
        // prevent that case.
        if (hiColorRAM != -1 && colorIndex == hiColorRAM)
        {
            key[y*4+x] = _hex[_colorRAM];
            return true;
        }

        // else
        std::vector<std::pair<int,int>> usedColors = {
            {0,0}, {0,1}, {0,2}, {0,3}, {0,4}, {0,5}, {0,6}, {0,7},
            {0,8}, {0,9}, {0,10}, {0,11}, {0,12}, {0,13}, {0,14}, {0,15}
        };

        for (int i=0; i<totalMasks; ++i)
        {
            if (mask && (1<<i))
            {
                int xdiff = masks[i][0];
                int ydiff = masks[i][1];

                int neighborColor = getValueFromKey(x+xdiff, y+ydiff, key);
                if (neighborColor != -1)
                    usedColors[neighborColor].first++;
            }
        }

        // use the most frequently color from neighbors
        std::sort(std::begin(usedColors), std::end(usedColors));
        int neighColor = usedColors[15].second;
        if (neighColor == _colorRAM ||
                neighColor == ui->widgetKoala->_d02xColors[0] ||
                neighColor == ui->widgetKoala->_d02xColors[1] ||
                neighColor == ui->widgetKoala->_d02xColors[2])
        {
            key[y*4+x] = _hex[neighColor];
            return true;
        }
    }
    return false;
}
void ImportKoalaDialog::normalizeWithNeighborStrategy(char* key, int hiColorRAM)
{
    quint8 masks[]
    {
        // 8
        0xff,       // 111-1_1-111: all positions

        // 4
        0x5a,       // 010-1_1-010: H-V
        0xa5,       // 101-0_0-101: Diag

        // 2
        0x18,       // 000-1_1-000: H
        0x42,       // 010-0_0-010: V
        0x81,       // 100-0_0-001: Dd
        0x24,       // 001-0_0-100: Du

        // 1
        0x40,       // 010-0_0-000: t
        0x10,       // 000-1_0-000: l
        0x08,       // 000-0_1-000: r
        0x02,       // 000-0_0-010: b
    };
    const int totalMasks = sizeof(masks) / sizeof(masks[0]);

    for (int maskIndex=0; maskIndex<totalMasks; maskIndex++)
    {
        bool keyChanged = false;
        do {
            keyChanged = false;
            for (int y=0; y<8; ++y)
            {
                for (int x=0; x<4; ++x)
                    keyChanged |= tryChangeKey(x, y, key, masks[maskIndex], hiColorRAM);
            }
        } while(keyChanged);
    }
}

int ImportKoalaDialog::getColorByPaletteProximity(int colorIndex, const std::vector<int>& colorsToFind)
{
    // FIXME:
    // better to have an static table instead of this "magic" heuristic.
    // In fact, I don't know if this heuristic is good enough or not

    // cycle colors taken from:
    // http://codebase64.org/doku.php?id=base:vic-ii_color_cheatsheet
    int cycle1[] = {0, 6, 0xb, 4, 0xe, 5, 3, 0xd, 1};
    const int cycle1Max = sizeof(cycle1) / sizeof(cycle1[0]);

    int cycle2[] = {0, 9, 2, 8, 0xc, 0xa, 0xf, 1};
    const int cycle2Max = sizeof(cycle2) / sizeof(cycle2[0]);

    int cycle3[] = {0, 6, 0xc, 0xf, 1};
    const int cycle3Max = sizeof(cycle3) / sizeof(cycle3[0]);

    int cycle4[] = {9, 6, 8, 0xc, 0xa, 0xf, 0xd};
    const int cycle4Max = sizeof(cycle4) / sizeof(cycle4[0]);

    int cycle5[] = {0, 6, 2, 4, 0xe, 5, 3, 7, 1};
    const int cycle5Max = sizeof(cycle5) / sizeof(cycle5[0]);

    std::vector<std::pair<int,int>> usedColors = {
        {0,0}, {0,1}, {0,2}, {0,3}, {0,4}, {0,5}, {0,6}, {0,7},
        {0,8}, {0,9}, {0,10}, {0,11}, {0,12}, {0,13}, {0,14}, {0,15}
    };

    struct {
        int *array;
        int arrayLength;
    } cycles[] = {
        {cycle1, cycle1Max},
        {cycle2, cycle2Max},
        {cycle3, cycle3Max},
        {cycle4, cycle4Max},
        {cycle5, cycle5Max},
    };
    const int cyclesMax = sizeof(cycles) / sizeof(cycles[0]);

    for (int i=0; i<cyclesMax; ++i)
    {
        // find indexColor;
        int idx = -1;
        for (int j=0; j<cycles[i].arrayLength; j++)
        {
            if (cycles[i].array[j] == colorIndex) {
                idx = j;
                break;
            }
        }
        // calculate distances
        if (idx != -1)
        {
            for (int j=0; j<cycles[i].arrayLength; j++)
            {
                int tmpColor = cycles[i].array[j];
                if (std::find(std::begin(colorsToFind), std::end(colorsToFind), tmpColor) != std::end(colorsToFind))
                {
                    usedColors[tmpColor].first += std::max(0, 9 - abs(idx-j));
                }
            }
        }
    }

    std::sort(std::begin(usedColors), std::end(usedColors));

    if (std::find(std::begin(colorsToFind), std::end(colorsToFind), usedColors[15].second) == std::end(colorsToFind))
    {
        qDebug() << "ouch";
    }
    return usedColors[15].second;
}

void ImportKoalaDialog::normalizeWithPaletteStrategy(char* key, int hiColorRAM)
{
    Q_UNUSED(hiColorRAM);

    for (int y=0; y<8; ++y)
    {
        for (int x=0; x<4; ++x)
        {
            int colorIndex = getValueFromKey(x, y, key);

            if (colorIndex != _colorRAM &&
                    colorIndex != ui->widgetKoala->_d02xColors[0] &&
                    colorIndex != ui->widgetKoala->_d02xColors[1] &&
                    colorIndex != ui->widgetKoala->_d02xColors[2])
            {
                int newColor = -1;
                const std::vector<int> colorsToFind = {ui->widgetKoala->_d02xColors[0],
                                                 ui->widgetKoala->_d02xColors[1],
                                                 ui->widgetKoala->_d02xColors[2],
                                                 _colorRAM};

                // Palette proximity Strategy
                newColor = getColorByPaletteProximity(colorIndex, colorsToFind);
                key[y*4+x] = _hex[newColor];
            }
        }
    }
}

void ImportKoalaDialog::normalizeKey(char* key, int hiColorRAM)
{
    if (ui->radioButtonNeighbor->isChecked())
        normalizeWithNeighborStrategy(key, hiColorRAM);
    else /* palette or random */
        normalizeWithPaletteStrategy(key, hiColorRAM);
}


bool ImportKoalaDialog::processChardef(const std::string& key, quint8* outKey, quint8* outColorRAM)
{
    // For the heuristic:
    // used colors that are not the same as d021, d022 and d023
    // vector<used_colors,color_index>
    std::vector<std::pair<int,int>> usedColors = {
        {0,0}, {0,1}, {0,2}, {0,3}, {0,4}, {0,5}, {0,6}, {0,7},
        {0,8}, {0,9}, {0,10}, {0,11}, {0,12}, {0,13}, {0,14}, {0,15}
    };

    // vector<x,y>
    std::vector<std::pair<int,int>> invalidCoords;

    // process valid colors
    for (int y=0; y<8; ++y)
    {
        quint8 bits = 0;
        for (int x=0; x<4; ++x)
        {
            int colorIndex = getValueFromKey(x,y,key.c_str());

            if (colorIndex == ui->widgetKoala->_d02xColors[0])
                ;
            else if (colorIndex == ui->widgetKoala->_d02xColors[1])
                bits |= (1 << (6-(x*2)));
            else if (colorIndex == ui->widgetKoala->_d02xColors[2])
                bits |= (2 << (6-(x*2)));
            else
                invalidCoords.push_back(std::make_pair(x,y));
            usedColors[colorIndex].first++;
        }
        outKey[y] = bits;
    }

    // return most frequently used ram color: reversed order, first
    std::sort(std::begin(usedColors), std::end(usedColors));
    std::reverse(std::begin(usedColors), std::end(usedColors));

    int hiColorRAM = _colorRAM = -1;
    _colorRAM = findColorRAM(usedColors, &hiColorRAM);

    // no colorRAM detected? That means that all colors are d020, d021, d022
    if (_colorRAM == -1)
    {
        Q_ASSERT(invalidCoords.size()==0 && "error in heuristic");
        // pick a random color for RAMcolor... like black
        _colorRAM = 0;
    }


    if (invalidCoords.size() > 0)
    {
        char copyKey[8*4 + 1];
        memcpy(copyKey, key.c_str(), sizeof(copyKey));
        copyKey[8*4] = 0;       // used when printing the key

        normalizeKey(copyKey, hiColorRAM);

        bool error = false;
        // by now, all invalid colors should have valid ones in the key
        for (auto& coord: invalidCoords)
        {
            auto x = coord.first;
            auto y = coord.second;

            int colorIndex = getValueFromKey(x, y, copyKey);

            if (colorIndex == ui->widgetKoala->_d02xColors[0])
                ;
            else if (colorIndex == ui->widgetKoala->_d02xColors[1])
                outKey[y] |= (1 << (6-(x*2)));
            else if (colorIndex == ui->widgetKoala->_d02xColors[2])
                outKey[y] |= (2 << (6-(x*2)));
            else if (colorIndex == _colorRAM)
                outKey[y] |= (3 << (6-(x*2)));
            else {
                error = true;
            }
        }
        if (error)
        {
            auto deb = qDebug();
            deb << "Key with error" \
                     << "key:" << key.c_str() \
                     << "new key:" << copyKey \
                     << "(" << ui->widgetKoala->_d02xColors[0] << "," \
                     << ui->widgetKoala->_d02xColors[1] << "," \
                     << ui->widgetKoala->_d02xColors[2] << ","\
                     << _colorRAM << ")" \
                     << "\n";

            for (auto& pair: usedColors)
            {
                 deb << pair.second << "=" << pair.first << ",";
            }

        }
    }

    *outColorRAM = _colorRAM + 8;

//    qDebug() << "key:" << key.c_str()
//             << "(" << ui->widgetKoala->_d02xColors[0] << ","
//             << ui->widgetKoala->_d02xColors[1] << ","
//             << ui->widgetKoala->_d02xColors[2] << ","
//             << _colorRAM << ")";

    return true;
}

void ImportKoalaDialog::convert()
{
    auto orig = ui->widgetKoala;
    auto conv = ui->widgetCharset;

    if (ui->radioD02xMostUsed->isChecked())
        orig->strategyD02xAny();
    else if (ui->radioD02xMostUsedHi->isChecked())
        orig->strategyD02xAbove8();
    else /* manual */
    {
        orig->_d02xColors[0] = ui->widgetD021->getColorIndex();
        orig->_d02xColors[1] = ui->widgetD022->getColorIndex();
        orig->_d02xColors[2] = ui->widgetD023->getColorIndex();
    }

    orig->reportResults();

    ui->lineEditUnique->setText(QString::number(orig->_uniqueChars.size()));

    for (int i=0; i<3; ++i)
        conv->_d02x[i] = orig->_d02xColors[i];
    ui->widgetD021->setColorIndex(orig->_d02xColors[0]);
    ui->widgetD022->setColorIndex(orig->_d02xColors[1]);
    ui->widgetD023->setColorIndex(orig->_d02xColors[2]);

    int charsetCount = 0;

    for (auto it = orig->_uniqueChars.begin(); it != orig->_uniqueChars.end(); ++it)
    {
        quint8 chardef[8];
        memset(chardef, 0, sizeof(chardef));

        quint8 colorRAM;
        processChardef(it->first, chardef, &colorRAM);

        conv->populateScreenAndColorRAM(it->second, charsetCount, colorRAM);
        conv->setCharset(charsetCount, chardef);

        charsetCount++;
    }

    conv->update();
}

void ImportKoalaDialog::on_radioForegroundMostUsed_clicked()
{
    convert();
}

void ImportKoalaDialog::on_radioForegroundMostUsedLow_clicked()
{
    convert();
}


void ImportKoalaDialog::on_radioD02xManual_clicked()
{
    convert();
}

void ImportKoalaDialog::on_radioD02xMostUsed_clicked()
{
    convert();
}

void ImportKoalaDialog::on_radioD02xMostUsedHi_clicked()
{
    convert();
}

void ImportKoalaDialog::on_radioButtonNeighbor_clicked()
{
    convert();
}

void ImportKoalaDialog::on_radioButtonPalette_clicked()
{
    convert();
}

void ImportKoalaDialog::on_checkBoxGrid_clicked()
{
    auto checked = ui->checkBoxGrid->isChecked();
    ui->widgetCharset->enableGrid(checked);
    ui->widgetKoala->enableGrid(checked);
}

void ImportKoalaDialog::on_pushButtonImport_clicked()
{
    auto state = new State(ui->widgetCharset->_charset, ui->widgetCharset->_colorRAMForChars, ui->widgetCharset->_screenRAM, QSize(40,25));

    state->setColorForPen(State::PEN_BACKGROUND, ui->widgetCharset->_d02x[0]);
    state->setColorForPen(State::PEN_MULTICOLOR1, ui->widgetCharset->_d02x[1]);
    state->setColorForPen(State::PEN_MULTICOLOR2, ui->widgetCharset->_d02x[2]);
    // FIXME
    state->setColorForPen(State::PEN_FOREGROUND, ui->widgetCharset->_colorRAMForChars[0]);
    state->setMulticolorMode(true);
    state->setCharColorMode(State::CHAR_COLOR_PER_CHAR);


    MainWindow::getInstance()->createDocument(state);

    // update last used dir
    QFileInfo info(ui->lineEdit->text());
    QSettings("RetroMoe","VChar64").setValue("dir/lastdir", info.absolutePath());

    accept();
}

void ImportKoalaDialog::on_pushButtonCancel_clicked()
{
    reject();
}

void ImportKoalaDialog::updateWidgets()
{
    QWidget* widgets[] =
    {
        ui->radioButtonNeighbor,
        ui->radioButtonPalette,
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
        ui->pushButtonImport
    };

    const int COUNT = sizeof(widgets) / sizeof(widgets[0]);

    for (int i=0; i<COUNT; i++)
    {
        widgets[i]->setEnabled(_validKoalaFile);
    }
}

void ImportKoalaDialog::mousePressEvent(QMouseEvent* event)
{
    ColorRectWidget *widgets[] = {
        ui->widgetD021,
        ui->widgetD022,
        ui->widgetD023
    };

    for (int i=0; i<3; i++)
    {
        auto localPos = widgets[i]->mapFromParent(event->pos());
        if (widgets[i]->isEnabled() && widgets[i]->rect().contains(localPos))
        {
            // will also trigger convert
            ui->radioD02xManual->setChecked(true);

            SelectColorDialog diag(this);
            diag.setCurrentColor(widgets[i]->getColorIndex());
            if (diag.exec())
            {
                widgets[i]->setColorIndex(diag.getSelectedColor());
                convert();
            }
            event->accept();
            return;
        }
    }
    event->ignore();
}
