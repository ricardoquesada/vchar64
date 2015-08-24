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
#include "commands.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"

BigChar::BigChar(QWidget *parent)
    : QWidget(parent)
    , _tileIndex(0)
    , _charIndex(0)
    , _cursorPos({0,0})
    , _tileProperties({{1,1},1})
    , _pixelSize({32,32})
    , _commandMergeable(false)
{
}

void BigChar::paintPixel(int x, int y, int selectedColor)
{
    auto&& state = State::getInstance();
    if (!state->isMultiColor() && selectedColor)
        selectedColor = 1;

    state->getUndoStack()->push(new PaintTileCommand(state, _tileIndex, QPoint(x,y), selectedColor, _commandMergeable));
}

void BigChar::mousePressEvent(QMouseEvent * event)
{
    event->accept();

    auto pos = event->localPos();

    int x = pos.x() / _pixelSize.width();
    int y = pos.y() / _pixelSize.height();
    if( x>=8*_tileProperties.size.width() || y>=8*_tileProperties.size.height())
        return;

    _cursorPos = {x,y};

    auto&& state = State::getInstance();
    int selectedColor = state->getSelectedColorIndex();

    if (event->button() == Qt::LeftButton)
        paintPixel(x, y, selectedColor);
    else if(event->button() == Qt::RightButton)
        paintPixel(x, y, 0);

    _commandMergeable = true;
}

void BigChar::mouseMoveEvent(QMouseEvent * event)
{
    event->accept();

    auto pos = event->localPos();

    int x = pos.x() / _pixelSize.width();
    int y = pos.y() / _pixelSize.height();
    if( x>=8*_tileProperties.size.width() || y>=8*_tileProperties.size.height())
        return;

    _cursorPos = {x,y};

    auto&& state = State::getInstance();
    int selectedColor = state->getSelectedColorIndex();

    auto&& button = event->buttons();
    if (button == Qt::LeftButton)
        paintPixel(x, y, selectedColor);
    else if(button == Qt::RightButton)
        paintPixel(x, y, 0);

    _commandMergeable = true;
}

void BigChar::mouseReleaseEvent(QMouseEvent * event)
{
    event->accept();

    // don't merge "tilePaint" command if the mouse was
    // released
    _commandMergeable = false;
}

void BigChar::keyPressEvent(QKeyEvent *event)
{
    event->accept();

    MainWindow *window = dynamic_cast<MainWindow*>(qApp->activeWindow());
    Ui::MainWindow *ui = window->getUi();

    auto&& state = State::getInstance();
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
        paintPixel(_cursorPos.x(), _cursorPos.y(), state->getSelectedColorIndex());
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
    _cursorPos = {qBound(0, _cursorPos.x(), 8*_tileProperties.size.width()-1),
                  qBound(0, _cursorPos.y(), 8*_tileProperties.size.height()-1)};
    update();
}


void BigChar::paintEvent(QPaintEvent *event)
{
    QPainter painter;
    painter.begin(this);

    // background
    painter.fillRect(event->rect(), QColor(204,204,204));

    QPen pen;
    pen.setColor({149,195,244,255});
    pen.setWidth(3);
    pen.setStyle(Qt::PenStyle::SolidLine);

    State *state = State::getInstance();
    quint8* charPtr = state->getCharAtIndex(_charIndex);

    for (int y=0; y<_tileProperties.size.height(); y++) {
        for (int x=0; x<_tileProperties.size.width(); x++) {
            QPoint tileToDraw(x,y);

            paintChar(painter, pen, charPtr, tileToDraw);
            // chars could be 64 chars away from each other
            charPtr +=  _tileProperties.interleaved * 8;
        }
    }

    paintSeparators(painter);
    paintFocus(painter);

    painter.end();
}

void BigChar::paintSeparators(QPainter &painter)
{
    QPen pen;
    pen.setColor({128,128,128,196});
    pen.setWidth(2);
    pen.setStyle(Qt::PenStyle::SolidLine);

    painter.setPen(pen);

    for (int x=1; x<=_tileProperties.size.width()-1; x++)
    {
        painter.drawLine(x*_pixelSize.width()*8, 0,
                         x*_pixelSize.width()*8, _pixelSize.height()*_tileProperties.size.height()*8);
    }
    for (int y=1; y<=_tileProperties.size.height()-1; y++)
    {
        painter.drawLine(0, y*_pixelSize.height()*8,
                        _pixelSize.width()*_tileProperties.size.width()*8, y*_pixelSize.height()*8);
    }

}

void BigChar::paintFocus(QPainter &painter)
{
    if (hasFocus())
    {
        QPen pen;
        pen.setColor({149,195,244,255});
        pen.setWidth(3);
        pen.setStyle(Qt::PenStyle::SolidLine);

        painter.setPen(pen);

        // vertical lines
        painter.drawLine(0, 0,
                         0, _tileProperties.size.height() * _pixelSize.height() * 8);
        painter.drawLine(_tileProperties.size.width() * _pixelSize.width() * 8, 0,
                         _tileProperties.size.width() * _pixelSize.width() * 8, _tileProperties.size.height() * _pixelSize.height() * 8);

        // horizontal lines
        painter.drawLine(0, 0,
                         _tileProperties.size.width() * _pixelSize.width() * 8, 0);
        painter.drawLine(0, _tileProperties.size.height() * _pixelSize.height() * 8,
                         _tileProperties.size.width() * _pixelSize.width() * 8, _tileProperties.size.height() * _pixelSize.height() * 8);
    }
}


void BigChar::paintChar(QPainter& painter, const QPen& pen, quint8 *charPtr, const QPoint& tileToDraw)
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
            // 'char' doesn't work Ok with << and >>

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

void BigChar::setTileIndex(int tileIndex)
{
    auto state = State::getInstance();

    if (_tileIndex != tileIndex) {
        _tileIndex = tileIndex;
        _charIndex = state->getCharIndexFromTileIndex(_tileIndex);
        update();
    }
}

void BigChar::updateTileProperties()
{
    auto state = State::getInstance();
    _tileProperties = state->getTileProperties();

    // keep aspect ratio
    int maxTileSize = qMax(_tileProperties.size.width(), _tileProperties.size.height());
    int minWidgetSize = qMin(size().width(), size().height());
    _pixelSize.setWidth(minWidgetSize / (8*maxTileSize));
    _pixelSize.setHeight(minWidgetSize / (8*maxTileSize));

//    _pixelSize.setWidth(size().width() / (8*_tileProperties.size.width()));
//    _pixelSize.setHeight(size().height() / (8*_tileProperties.size.height()));

    update();
}

void BigChar::resizeEvent(QResizeEvent* event)
{
    (void)event;

    // keep aspect ratio
    int maxTileSize = qMax(_tileProperties.size.width(), _tileProperties.size.height());
    int minWidgetSize = qMin(size().width(), size().height());
    _pixelSize.setWidth(minWidgetSize / (8*maxTileSize));
    _pixelSize.setHeight(minWidgetSize / (8*maxTileSize));

//    _pixelSize.setWidth(size().width() / (8*_tileProperties.size.width()));
//    _pixelSize.setHeight(size().height() / (8*_tileProperties.size.height()));
}

