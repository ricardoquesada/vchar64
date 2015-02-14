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

static const int PIXEL_SIZE = 32;

BigChar::BigChar(QWidget *parent)
    : QWidget(parent)
    , _index(0)
    , _cursorPos({0,0})
{
    setFixedSize(PIXEL_SIZE * 8, PIXEL_SIZE * 8);
}

void BigChar::paintPixel(int x, int y)
{
    int bitIndex = x + y * 8;

    State *state = State::getInstance();
    int selectedColor = state->getSelectedColorIndex();

    if (!state->isMultiColor() && selectedColor)
        selectedColor = 1;

    state->setCharColor(_index, bitIndex, selectedColor);

    dynamic_cast<QWidget*>(parent())->update();
}

void BigChar::mousePressEvent(QMouseEvent * event)
{
    auto pos = event->localPos();

    int x = pos.x() / PIXEL_SIZE;
    int y = pos.y() / PIXEL_SIZE;
    if( x>=8 || y>=8)
        return;

    _cursorPos = {x,y};

    paintPixel(x, y);
}

void BigChar::mouseMoveEvent(QMouseEvent * event)
{
    auto pos = event->localPos();

    int x = pos.x() / PIXEL_SIZE;
    int y = pos.y() / PIXEL_SIZE;
    if( x>=8 || y>=8)
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
    _cursorPos = {qBound(0, _cursorPos.x(), 7),
                  qBound(0, _cursorPos.y(), 7)};
    update();
}


void BigChar::paintEvent(QPaintEvent *event)
{
    QPainter painter;
    painter.begin(this);

    State *state = State::getInstance();

    // background
    painter.fillRect(event->rect(), QColor(204,204,204));


    u_int8_t* charPtr = State::getInstance()->getCharAtIndex(_index);

    int end_x = 8;
    int pixel_size_x = PIXEL_SIZE;
    int increment_x = 1;
    int bits_to_mask = 1;

    if (state->isMultiColor())
    {
        end_x = 4;
        pixel_size_x = PIXEL_SIZE * 2;
        increment_x = 2;
        bits_to_mask = 3;
    }

    QPen pen;
    pen.setColor({128,128,255});
    pen.setWidth(3);
    pen.setStyle(Qt::PenStyle::SolidLine);

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

            if (hasFocus() && y==_cursorPos.y() && x==_cursorPos.x()/increment_x)
                painter.setPen(pen);
            else
                painter.setPen(Qt::PenStyle::NoPen);

            painter.drawRect(x * pixel_size_x, y * PIXEL_SIZE, pixel_size_x-1, PIXEL_SIZE-1);
        }
    }

    painter.end();
}

void BigChar::setIndex(int index)
{
    if (_index != index) {
        _index = index;
        emit indexChanged(_index);
        update();
    }
}

