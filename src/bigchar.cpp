/****************************************************************************
Copyright 2015 Ricardo Quesada

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

#include "bigchar.h"

#include <algorithm>
#include <QPainter>
#include <QPaintEvent>

#include "state.h"
#include "constants.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"

static const int WIDGET_WIDTH = 256;
static const int WIDGET_HEIGHT = 256;

BigChar::BigChar(QWidget *parent)
    : QWidget(parent)
    , _charIndex(0)
    , _cursorPos({0,0})
    , _tileSize({1,1})
    , _charInterleaved(1)
    , _pixelSize({32,32})
{
    setFixedSize(WIDGET_WIDTH, WIDGET_HEIGHT);
}

void BigChar::paintPixel(int x, int y)
{
    int bitIndex = (x%8) + (y%8) * 8;

    State *state = State::getInstance();
    int selectedColor = state->getSelectedColorIndex();

    if (!state->isMultiColor() && selectedColor)
        selectedColor = 1;

    int charIndex = _charIndex + (x/8) * _charInterleaved + (y/8) * _charInterleaved * _tileSize.width();
    state->setCharColor(charIndex, bitIndex, selectedColor);

    dynamic_cast<QWidget*>(parent())->update();
}

void BigChar::mousePressEvent(QMouseEvent * event)
{
    auto pos = event->localPos();

    int x = pos.x() / _pixelSize.width();
    int y = pos.y() / _pixelSize.height();
    if( x>=8*_tileSize.width() || y>=8*_tileSize.height())
        return;

    _cursorPos = {x,y};

    paintPixel(x, y);
}

void BigChar::mouseMoveEvent(QMouseEvent * event)
{
    auto pos = event->localPos();

    int x = pos.x() / _pixelSize.width();
    int y = pos.y() / _pixelSize.height();
    if( x>=8*_tileSize.width() || y>=8*_tileSize.height())
        return;

    _cursorPos = {x,y};

    paintPixel(x, y);
}

void BigChar::keyPressEvent(QKeyEvent *event)
{
    MainWindow *window = dynamic_cast<MainWindow*>(qApp->activeWindow());
    Ui::MainWindow *ui = window->getUi();

    auto state = State::getInstance();
    int increment_x = state->isMultiColor() ? 2 : 1;

    switch (event->key()) {
    case Qt::Key_Left:
        _cursorPos += {-increment_x,0};
        break;
    case Qt::Key_Right:
        _cursorPos += {+increment_x,0};
        break;
    case Qt::Key_Down:
        _cursorPos += {0,+1};
        break;
    case Qt::Key_Up:
        _cursorPos += {0,-1};
        break;
    case Qt::Key_Space:
        paintPixel(_cursorPos.x(), _cursorPos.y());
        break;
    case Qt::Key_1:
        ui->radioButton_1->click();
        break;
    case Qt::Key_2:
        ui->radioButton_2->click();
        break;
    case Qt::Key_3:
        ui->radioButton_3->click();
        break;
    case Qt::Key_4:
        ui->radioButton_4->click();
        break;
    case Qt::Key_M:
        ui->checkBox->click();
        break;
    case Qt::Key_Minus:
        ui->spinBox->setValue(ui->spinBox->value() - 1);
        break;
    case Qt::Key_Plus:
        ui->spinBox->setValue(ui->spinBox->value() + 1);
        break;
    default:
        QWidget::keyPressEvent(event);
    }
    _cursorPos = {qBound(0, _cursorPos.x(), 8*_tileSize.width()-1),
                  qBound(0, _cursorPos.y(), 8*_tileSize.height()-1)};
    update();
}


void BigChar::paintEvent(QPaintEvent *event)
{
    QPainter painter;
    painter.begin(this);

    // background
    painter.fillRect(event->rect(), QColor(204,204,204));

    QPen pen;
    pen.setColor({128,128,255});
    pen.setWidth(3);
    pen.setStyle(Qt::PenStyle::SolidLine);

    State *state = State::getInstance();
    u_int8_t* charPtr = state->getCharAtIndex(_charIndex);

    for (int y=0; y<_tileSize.height(); y++) {
        for (int x=0; x<_tileSize.width(); x++) {
            QPoint tileToDraw(x,y);

            paintChar(painter, pen, charPtr, tileToDraw);
            // chars are 64 chars away from each other
            charPtr += _charInterleaved * 8;
        }
    }

    painter.end();
}

void BigChar::paintChar(QPainter& painter, const QPen& pen, u_int8_t* charPtr, const QPoint& tileToDraw)
{
    State *state = State::getInstance();

    int end_x = 8;
    int pixel_size_x = _pixelSize.width();
    int increment_x = 1;
    int bits_to_mask = 1;

    if (state->isMultiColor())
    {
        end_x = 4;
        pixel_size_x = _pixelSize.width() * 2;
        increment_x = 2;
        bits_to_mask = 3;
    }

    for (int y=0; y<8; y++) {

        char letter = charPtr[y];

        for (int x=0; x<end_x; x++) {

            // Warning: Don't use 'char'. Instead use 'unsigned char'
            // to prevent a bug in the compiler (or in my code???)

            // only mask the bits are needed
            unsigned char mask = bits_to_mask << (((end_x-1)-x) * increment_x);

            unsigned char color = letter & mask;
            // now transform those bits into values from 0-3 since those are the
            // possible colors

            int bits_to_shift = (((end_x-1)-x) * increment_x);
            int color_index = color >> bits_to_shift;

            if (!state->isMultiColor() && color_index )
                color_index = 3;
            painter.setBrush(Constants::CBMcolors[state->getColorAtIndex(color_index)]);

            if (hasFocus()
                    && (x + tileToDraw.x() * 8 / increment_x) == _cursorPos.x() / increment_x
                    && (y + tileToDraw.y( ) * 8) == _cursorPos.y()
                    )
                painter.setPen(pen);
            else
                painter.setPen(Qt::PenStyle::NoPen);

            painter.drawRect(x * pixel_size_x + _pixelSize.width() * 8 * tileToDraw.x(),
                             y * _pixelSize.height() + _pixelSize.height() * 8 * tileToDraw.y(),
                             pixel_size_x-1,
                             _pixelSize.height()-1);
        }
    }
}

void BigChar::setIndex(int tileIndex)
{
    auto state = State::getInstance();
    int charIndex = state->charIndexFromTileIndex(tileIndex);

    if (_charIndex != charIndex) {
        _charIndex = charIndex;
        emit indexChanged(_charIndex);
        update();
    }
}

void BigChar::updateTileProperties()
{
    auto state = State::getInstance();
    _tileSize = state->getTileSize();
    _charInterleaved = state->getCharInterleaved();
    _pixelSize.setWidth(WIDGET_WIDTH / (8*_tileSize.width()));
    _pixelSize.setHeight(WIDGET_HEIGHT / (8*_tileSize.height()));
    update();
}

