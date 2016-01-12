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

#include <QFileDialog>
#include <QSettings>
#include <QDebug>

ImportKoalaDialog::ImportKoalaDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ImportKoalaDialog)
{
    ui->setupUi(this);

    auto lastDir = QSettings("RetroMoe","VChar64").value("dir/lastdir").toString();
    ui->lineEdit->setText(lastDir);
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
        }
    }
}


// helpers


bool ImportKoalaDialog::validateKoalaFile(const QString& filepath)
{
    QFileInfo info(filepath);

    if (info.exists() && info.isFile() && (info.size() == 10003 || info.size() == 10002))
    {
        ui->widgetKoala->loadKoala(filepath);
        return true;
    }
    return false;
}

int ImportKoalaDialog::pass1(const std::vector<std::pair<int,int>>& usedColors)
{
    for (int i=0; i<16; i++)
    {
        const int conv_colors[] = {-1, -1, -1, -1, -1, -1, -1, -1,
                                   -1, -1, -1, -1, -1,  5,  6, -1};

        auto color = usedColors[i].second;
        if (color != ui->widgetKoala->_d02xColors[0] &&
                color != ui->widgetKoala->_d02xColors[1] &&
                color != ui->widgetKoala->_d02xColors[2])
        {
            if (color < 8)
            {
                return color;
            }
            else
            {
                if (conv_colors[color] != -1)
                {
                    return conv_colors[color];
                }
            }
            return -1;
        }
    }
    return -1;
}

int ImportKoalaDialog::pass2(const std::vector<std::pair<int,int>>& usedColors)
{
    for (int i=0; i<16; i++)
    {
        const int conv_colors[] = {-1, -1, -1, -1, -1, -1, -1, -1,
                                    2,  2,  2,  0,  1, -1, -1,  1};

        auto color = usedColors[i].second;
        if (color != ui->widgetKoala->_d02xColors[0] &&
                color != ui->widgetKoala->_d02xColors[1] &&
                color != ui->widgetKoala->_d02xColors[2])
        {
            return conv_colors[color];
        }
        return -1;
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

bool ImportKoalaDialog::tryChangeKey(int x, int y, char* key, quint8 mask)
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
        std::vector<std::pair<int,int>> usedColors;
        for (int i=0; i<16; i++)
            usedColors.push_back(std::make_pair(0,i));

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

        // use the color the most frequently color used by the neighbors
        std::sort(std::begin(usedColors), std::end(usedColors));
        int neighColor = usedColors[15].second;
        if (neighColor == _colorRAM ||
                neighColor == ui->widgetKoala->_d02xColors[0] ||
                neighColor == ui->widgetKoala->_d02xColors[1] ||
                neighColor == ui->widgetKoala->_d02xColors[2])
        {
            static const char* hex ="0123456789ABCDEF";
            key[y*4+x] = hex[neighColor];
            return true;
        }
    }
    return false;
}

void ImportKoalaDialog::simplifyKey(char* key)
{
    quint8 masks[]
    {
        0xff,       // 111-1_1-111: all positions
        0x5a,       // 010-1_1-010: H-V
        0xa5,       // 101-0_0-101: Diag
        0x18,       // 000-1_1-000: H
        0x42,       // 010-0_0-010: V
        0x81,       // 100-0_0-001: Dd
        0x24,       // 001-0_0-100: Du
    };
    const int totalMasks = sizeof(masks) / sizeof(masks[0]);

    for (int maskIndex=0; maskIndex<totalMasks; maskIndex++)
    {
        bool keyChanged = false;
        do {
            for (int y=0; y<8; ++y)
            {
                for (int x=0; x<4; ++x)
                {
                    keyChanged = tryChangeKey(x, y, key, masks[maskIndex]);
                }
            }
        } while(keyChanged);
    }
}

bool ImportKoalaDialog::processChardef(const std::string& key, quint8* outKey, quint8* outColorRAM)
{
    // For the heuristic:
    // used colors that are not the same as d021, d022 and d023
    // vector<used_colors,color_index>
    std::vector<std::pair<int,int>> usedColors;
    for (int i=0; i<16; i++)
        usedColors.push_back(std::make_pair(0,i));

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


    // 1st pass: assign to colorRAM the most used color that is not d021,d022,d023
    //  - if color is <8
    //  - or color use non-aggressive conversion table
    int colorConvertedToRAMColor = pass1(usedColors);

    // 2nd pass: if 1st pass failed, use agressive conversion table
    if (colorConvertedToRAMColor == -1)
        colorConvertedToRAMColor = pass2(usedColors);

    // 3rd pass: no colorRAM is needed. All colors are d020, d021, d022
    if (colorConvertedToRAMColor == -1)
    {
        // this is valid only if there are no invalidCoords
        Q_ASSERT(invalidCoords.size() == 0 && "Error in heuristic logic");
        // pick a random color for RAMcolor... like black
        colorConvertedToRAMColor = 0;
    }

    _colorRAM = colorConvertedToRAMColor + 8;

    if (invalidCoords.size() > 0)
    {
        char copyKey[8*4];
        memcpy(copyKey, key.c_str(), sizeof(copyKey));

        simplifyKey(copyKey);

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
            else if (colorIndex == colorConvertedToRAMColor)
                outKey[y] |= (3 << (6-(x*2)));
            else {
                qDebug() << "Ouch... key not converted yet at (" << x << "," << y << ") " << colorIndex << " key: " << key.c_str();
            }
        }
    }

    *outColorRAM = _colorRAM;

    return true;
}

void ImportKoalaDialog::on_pushButtonConvert_clicked()
{
    auto orig = ui->widgetKoala;
    auto conv = ui->widgetCharset;

    if (ui->radioMostUsedColors->isChecked())
        orig->strategyHiColorsUseFixedColors();
    else
        orig->strategyMostUsedColorsUseFixedColors();
    orig->reportResults();


    for (int i=0; i<3; ++i)
        conv->_d02x[i] = orig->_d02xColors[i];

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
