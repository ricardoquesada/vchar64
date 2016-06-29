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

#include "importkoalabitmapwidget.h"

#include <functional>

#include <QPainter>
#include <QPaintEvent>
#include <QFile>
#include <QDebug>
#include <QGuiApplication>

#include "palette.h"
#include "state.h"
#include "mainwindow.h"

static const int PIXEL_SIZE = 1;
static const int COLUMNS = 40;
static const int ROWS = 25;
static const int OFFSET = 0;

ImportKoalaBitmapWidget::ImportKoalaBitmapWidget(QWidget *parent)
    : QWidget(parent)
    , _displayGrid(false)
    , _selecting(false)
    , _selectingSize({0,0})
    , _cursorPos({0,0})
{
    memset(_framebuffer, 0, sizeof(_framebuffer));
    setFixedSize(PIXEL_SIZE * COLUMNS * 8 + OFFSET * 2,
                 PIXEL_SIZE * ROWS * 8 + OFFSET * 2);
}

//
// Overriden
//
void ImportKoalaBitmapWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter;

    painter.begin(this);
    painter.fillRect(event->rect(), QWidget::palette().color(QWidget::backgroundRole()));

    painter.setBrush(QColor(0,0,0));
    painter.setPen(Qt::NoPen);

    for (int y=0; y<200; y++)
    {
        for (int x=0; x<160; ++x)
        {
            painter.setBrush(Palette::getColor(_framebuffer[y * 160 + x]));
            painter.drawRect((x*2) * PIXEL_SIZE + OFFSET,
                             y * PIXEL_SIZE + OFFSET,
                             PIXEL_SIZE * 2,
                             PIXEL_SIZE);
        }
    }

    if (_displayGrid)
    {
        auto pen = painter.pen();
        pen.setColor(QColor(0,128,0));
        pen.setStyle(Qt::DotLine);
        painter.setPen(pen);

        for (int y=0; y<=200; y=y+8)
            painter.drawLine(QPointF(0,y), QPointF(320,y));

        for (int x=0; x<=320; x=x+8)
            painter.drawLine(QPointF(x,0), QPointF(x,200));
    }

    if (_selecting)
    {
        auto pen = painter.pen();
        pen.setColor({149,195,244,255});
        painter.setPen(pen);
        painter.setBrush(QColor(149,195,244,64));
        painter.drawRect(_cursorPos.x() * 8 * PIXEL_SIZE + OFFSET,
                         _cursorPos.y() * 8 * PIXEL_SIZE + OFFSET,
                         _selectingSize.width() * 8 * PIXEL_SIZE,
                         _selectingSize.height() * 8 * PIXEL_SIZE);
    }

    painter.end();
}

void ImportKoalaBitmapWidget::mousePressEvent(QMouseEvent * event)
{
    event->accept();

    auto pos = event->localPos();

    int x = (pos.x() - OFFSET) / PIXEL_SIZE / 8;
    int y = (pos.y() - OFFSET) / PIXEL_SIZE / 8;

    // sanity check
    x = qBound(0, x, COLUMNS - 1);
    y = qBound(0, y, ROWS - 1);

    if (event->button() == Qt::LeftButton)
    {
        if (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier)
        {
            // Click + Shift == select mode
            _selecting = true;

            if (x >= _cursorPos.x())
                x++;
            if (y >= _cursorPos.y())
                y++;

            _selectingSize = {x - _cursorPos.x(),
                              y - _cursorPos.y()};

            // sanity check
            _selectingSize = {
                qBound(-_cursorPos.x(), _selectingSize.width(), COLUMNS - _cursorPos.x()),
                qBound(-_cursorPos.y(), _selectingSize.height(), ROWS - _cursorPos.y())
            };
        }
        else
        {
            // click without shift == select single char and clear select mode
            if (_selecting)
            {
                _selecting = false;
                _selectingSize = {1,1};
            }
            _cursorPos = {x,y};
        }

        emit selectedRegionUpdated(getSelectedRegion());

        update();
    }
}

void ImportKoalaBitmapWidget::mouseMoveEvent(QMouseEvent * event)
{
    event->accept();

    if (event->buttons() == Qt::LeftButton)
    {
        auto pos = event->localPos();
        int x = (pos.x() - OFFSET) / PIXEL_SIZE / 8;
        int y = (pos.y() - OFFSET) / PIXEL_SIZE / 8;

        if (x >= _cursorPos.x())
            x++;
        if (y >= _cursorPos.y())
            y++;

        _selectingSize = {x - _cursorPos.x(),
                          y - _cursorPos.y()};

        // sanity check
        _selectingSize = {
            qBound(-_cursorPos.x(), _selectingSize.width(), COLUMNS-_cursorPos.x()),
            qBound(-_cursorPos.y(), _selectingSize.height(), ROWS-_cursorPos.y())
        };

        _selecting = true;

        emit selectedRegionUpdated(getSelectedRegion());
        update();
    }
}

//
// public
//
void ImportKoalaBitmapWidget::loadKoala(const QString& koalaFilepath)
{
    // in case the loaded file has less bytes than required, fill the buffer with zeroes
    memset(&_koala, 0, sizeof(_koala));

    QFile file(koalaFilepath);
    file.open(QIODevice::ReadOnly);
    file.read((char*)&_koala, sizeof(_koala));

    toFrameBuffer();

    parseKoala();
}

void ImportKoalaBitmapWidget::parseKoala()
{
    resetColors();
    findUniqueCells();
}

void ImportKoalaBitmapWidget::enableGrid(bool enabled)
{
    if (_displayGrid != enabled)
    {
        _displayGrid = enabled;
        update();
    }
}

QRect ImportKoalaBitmapWidget::getSelectedRegion() const
{
    QRect region = {0, 0, COLUMNS, ROWS};
    if (_selecting)
    {
        if (_selectingSize.width() < 0)
        {
            region.setX(_cursorPos.x() + _selectingSize.width());
            region.setWidth(-_selectingSize.width());
        }
        else
        {
            region.setX(_cursorPos.x());
            region.setWidth(_selectingSize.width());
        }

        if (_selectingSize.height() < 0)
        {
            region.setY(_cursorPos.y() + _selectingSize.height());
            region.setHeight(-_selectingSize.height());
        }
        else
        {
            region.setY(_cursorPos.y());
            region.setHeight(_selectingSize.height());
        }
    }
    return region;
}

//
// protected
//
void ImportKoalaBitmapWidget::resetColors()
{
    // reset state
    _colorsUsed.clear();
    for (int i=0; i<16; i++)
        _colorsUsed.push_back(std::make_pair(0,i));
    _uniqueCells.clear();

    for (int i=0; i<3; i++)
        _d02xColors[i] = -1;
}

void ImportKoalaBitmapWidget::findUniqueCells()
{
    static const char hex[] = "0123456789ABCDEF";

    auto region = getSelectedRegion();

    for (int y=region.y(); y < region.y() + region.height(); ++y)
    {
        for (int x=region.x(); x < region.x() + region.width(); ++x)
        {
            // 8 * 4
            char key[33];
            key[32] = 0;

            for (int i=0; i<8; ++i)
            {
                for (int j=0; j<4; ++j)
                {
                    quint8 colorIndex = _framebuffer[(y * 8 + i) * 160 + (x * 4 + j)];
                    Q_ASSERT(colorIndex<16 && "Invalid color");
                    key[i*4+j] = hex[colorIndex];
                    _colorsUsed[colorIndex].first++;
                }
            }
            std::string skey(key);
            Q_ASSERT(skey.size() == 8*4 && "Invalid Key");

            if (_uniqueCells.find(skey) == _uniqueCells.end())
            {
                std::vector<std::pair<int,int>> v;
                v.push_back(std::make_pair(x,y));
                _uniqueCells[skey] = v;
            }
            else
            {
                _uniqueCells[skey].push_back(std::make_pair(x,y));
            }
        }
    }

    // FIXME: descending sort... just pass a "greater" function instead of reversing the result
    std::sort(std::begin(_colorsUsed), std::end(_colorsUsed));
    std::reverse(std::begin(_colorsUsed), std::end(_colorsUsed));
}

void ImportKoalaBitmapWidget::toFrameBuffer()
{
    // 25 rows
    for (int y=0; y<ROWS; ++y)
    {
        // 40 cols
        for (int x=0; x<COLUMNS; ++x)
        {
            // 8 pixels Y
            for (int i=0; i<8; ++i)
            {
                quint8 byte = _koala.bitmap[(y * COLUMNS + x) * 8 + i];

                static const quint8 masks[] = {192, 48, 12, 3};
                // 4 wide-pixels X
                for (int j=0; j<4; ++j)
                {
                    quint8 colorIndex = 0;
                    // get the two bits that reprent the color
                    quint8 color = byte & masks[j];
                    color >>= 6-j*2;

                    switch (color)
                    {
                    // bitmask 00: background ($d021)
                    case 0x0:
                        colorIndex = _koala.backgroundColor & 0x0f;
                        break;

                    // bitmask 01: #4-7 screen ram
                    case 0x1:
                        colorIndex = _koala.screenRAM[y * COLUMNS + x] >> 4;
                        break;

                    // bitmask 10: #0-3 screen ram
                    case 0x2:
                        colorIndex = _koala.screenRAM[y * COLUMNS + x] & 0xf;
                        break;

                    // bitmask 11: color ram
                    case 0x3:
                        colorIndex = _koala.colorRAM[y * COLUMNS + x] & 0xf;
                        break;
                    default:
                        qDebug() << "ImportKoalaWidget::paintEvent Invalid color: " << color << " at x,y=" << x << y;
                        break;
                    }

                    Q_ASSERT(colorIndex<16 && "Invalid colorIndex");

                    _framebuffer[(y * 8 + i) * 160 + (x * 4 + j)] = colorIndex;
                }
            }
        }
    }

    update();
}

void ImportKoalaBitmapWidget::reportResults()
{
    int validCells = 0;
    int invalidCells = 0;
    int validUniqueCells = 0;
    int invalidUniqueCells = 0;

    for (auto it=_uniqueCells.begin(); it!=_uniqueCells.end(); ++it)
    {
        bool keyIsValid = true;
        auto key = it->first;
        // key is 4 * 32 bytes long. Each element of
        // the key, is a pixel
        for (int i=0; i<(int)key.size(); i++)
        {
            // convert Hex to int
            char c = key[i] - '0';
            if (c > 9)
                c -= 7;         // 'A' - '9'

            int color = c;

            // determine whether or not the char can be drawn with current selected colors

            // c in d021/d022/d023?
            if (std::find(std::begin(_d02xColors), std::end(_d02xColors), color) != std::end(_d02xColors))
                continue;

            if (c<8)
            {
                keyIsValid = false;
                break;
            }
        }

        if (keyIsValid)
        {
            // it->second is the vector<pair<int,int>>
            validCells += (int)it->second.size();
            validUniqueCells++;
        }
        else
        {
            // it->second is the vector<pair<int,int>>
            invalidCells += (int)it->second.size();
            invalidUniqueCells++;
        }
    }

    qDebug() << "Valid cells: " << validCells << " Valid Unique cells: " << validUniqueCells;
    qDebug() << "Invalid cells:" << invalidCells << " Invalid Unique cells: " << invalidUniqueCells;
    qDebug() << "$d021,22,23=" << _d02xColors[0] << _d02xColors[1] << _d02xColors[2];

    auto deb = qDebug();
    for (int i=0; i<16; i++)
         deb << "Color:" << _colorsUsed[i].second << "=" << _colorsUsed[i].first << "\n";
}

void ImportKoalaBitmapWidget::strategyD02xAbove8()
{
    // the most used colors whose values are >=8 are going to be used for d021, d022 and d023
    // if can't find 3 colors, list is completed with most used colors whose value is < 8

    // colors are already sorted: use most used colors whose values is >= 8
    // values < 8 are reserved screen color

    int found = 0;
    for (auto& color: _colorsUsed)
    {
        if (color.second >= 8 && color.first > 0) {
            _d02xColors[found++] = color.second;
            if (found == 3)
                break;
        }
    }

    // make sure that 3 colors where selected.
    // if not complete the list with most used colors where color < 8
    for (int j=0,i=0; i<3-found; ++i, ++j)
    {
        if (_colorsUsed[j].second < 8 && _colorsUsed[j].first > 0)
        {
            _d02xColors[found++] = _colorsUsed[j].second;
        }
    }
}

void ImportKoalaBitmapWidget::strategyD02xAny()
{
    // three most used colors are the ones to be used for d021, d022 and d023
    for (int i=0; i<3; ++i)
        _d02xColors[i] = _colorsUsed[i].second;
}

