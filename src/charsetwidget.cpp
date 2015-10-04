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

#include "charsetwidget.h"

#include <QPainter>
#include <QPaintEvent>

#include "palette.h"
#include "state.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"

static const int PIXEL_SIZE = 2;
static const int COLUMNS = 32;
static const int ROWS = 8;

CharSetWidget::CharSetWidget(QWidget *parent)
    : QWidget(parent)
    , _cursorPos({0,0})
    , _selecting(false)
    , _selectingSize({1,1})
    , _charIndex(-1)
{
    setFixedSize(PIXEL_SIZE * COLUMNS * 8, PIXEL_SIZE * ROWS * 8);
}

void CharSetWidget::updateCharIndex(int charIndex)
{
    if (_charIndex != charIndex)
    {
        _charIndex = charIndex;

        auto state = State::getInstance();
        int tileIndex = state->getTileIndexFromCharIndex(charIndex);

        // in this order
        emit tileSelected(tileIndex);
        emit charSelected(charIndex);
    }
}

void CharSetWidget::mousePressEvent(QMouseEvent * event)
{
    event->accept();

    auto pos = event->localPos();

    int x = pos.x() / PIXEL_SIZE / 8;
    int y = pos.y() / PIXEL_SIZE / 8;
    int charIndex = x + y * COLUMNS;

    if (event->button() == Qt::LeftButton)
    {
        if (_selecting)
        {
            _selecting = false;
            _selectingSize = {1,1};
        }

        updateCharIndex(charIndex);
        update();
    }
}

void CharSetWidget::mouseMoveEvent(QMouseEvent * event)
{
    event->accept();

    if (event->buttons() == Qt::LeftButton)
    {
        auto pos = event->localPos();
        int x = pos.x() / PIXEL_SIZE / 8;
        int y = pos.y() / PIXEL_SIZE / 8;

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

        update();
    }
}

void CharSetWidget::keyPressEvent(QKeyEvent *event)
{
    event->accept();

    QPoint point;
    switch (event->key()) {
    case Qt::Key_Left:
        point = {-1,0};
        break;
    case Qt::Key_Right:
        point = {+1,0};
        break;
    case Qt::Key_Down:
        point = {0,+1};
        break;
    case Qt::Key_Up:
        point = {0,-1};
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }

    bool selecting = (event->modifiers() & Qt::ShiftModifier);

    // disabling selecting?
    if (_selecting && !selecting) {
        _selectingSize = {1,1};
    }
    else
    {
        if (selecting)
        {
            _selectingSize += {point.x(), point.y()};

            if (_selectingSize.width() == 0)
                _selectingSize.setWidth(_selectingSize.width() + 1 * point.x());
            if (_selectingSize.height() == 0)
                _selectingSize.setHeight(_selectingSize.height() + 1 * point.y());

            _selectingSize = {
                qBound(-_cursorPos.x(), _selectingSize.width(), COLUMNS-_cursorPos.x()),
                qBound(-_cursorPos.y(), _selectingSize.height(), ROWS-_cursorPos.y())
            };
        }
        else
        {
            _cursorPos += point;
            _cursorPos = {qBound(0, _cursorPos.x(), COLUMNS-1),
                          qBound(0, _cursorPos.y(), ROWS-1)};

            // reverse calculation. From char index to tile index
            int charIndex = _cursorPos.x() + _cursorPos.y() * COLUMNS;
            updateCharIndex(charIndex);
        }
    }

    _selecting = selecting;
    update();
}

void CharSetWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter;

    painter.begin(this);
//    painter.fillRect(event->rect(), QBrush(QColor(255, 255, 255)));
    painter.fillRect(event->rect(), QColor(204,204,204));

    painter.setBrush(QColor(0,0,0));
    painter.setPen(Qt::NoPen);

    auto state = State::getInstance();

    int end_x = 8;
    int pixel_size_x = PIXEL_SIZE;
    int increment_x = 1;
    int bits_to_mask = 1;

    if (state->shouldBeDisplayedInMulticolor())
    {
        end_x = 4;
        pixel_size_x = PIXEL_SIZE * 2;
        increment_x = 2;
        bits_to_mask = 3;
    }

    QPen pen;
    pen.setColor({149,195,244,255});
    if (hasFocus())
        pen.setWidth(3);
    else
        pen.setWidth(1);
    pen.setStyle(Qt::PenStyle::SolidLine);

    for (int w=0; w<COLUMNS; w++) {
        for (int h=0; h<ROWS; h++) {

            int index = w + h * COLUMNS;
            quint8* charPtr = state->getCharAtIndex(index);

            for (int y=0; y<8; y++) {

                char letter = charPtr[y];

                for (int x=0; x<end_x; x++) {

                    // Warning: Don't use 'char'. Instead use 'unsigned char'.
                    // 'char' doesn't work Ok with << and >>
                    // only mask the bits are needed
                    unsigned char mask = bits_to_mask << (((end_x-1)-x) * increment_x);

                    unsigned char color = letter & mask;
                    // now transform those bits into values from 0-3 since those are the
                    // possible colors

                    int bits_to_shift = (((end_x-1)-x) * increment_x);
                    int color_pen = color >> bits_to_shift;

                    if (!state->shouldBeDisplayedInMulticolor() && color_pen )
                        color_pen = State::PEN_FOREGROUND;
                    painter.setBrush(Palette::getColorForPen(color_pen));
                    painter.drawRect((w*end_x+x) * pixel_size_x, (h*8+y) * PIXEL_SIZE, pixel_size_x, PIXEL_SIZE);
                }
            }

            painter.setPen(Qt::NoPen);
        }
    }

    if (_selecting) {
        pen.setColor({149,195,244,255});
        painter.setPen(pen);
        painter.setBrush(QColor(149,195,244,64));
        painter.drawRect(_cursorPos.x()*8*PIXEL_SIZE, _cursorPos.y()*8*PIXEL_SIZE,
                         _selectingSize.width()*8*PIXEL_SIZE, _selectingSize.height()*8*PIXEL_SIZE);
    }
    else
    {
        pen.setColor({149,195,244,255});
        painter.setPen(pen);
        painter.setBrush(QColor(128,0,0,0));
        painter.drawRect(_cursorPos.x()*8*PIXEL_SIZE, _cursorPos.y()*8*PIXEL_SIZE,
                         8*PIXEL_SIZE, 8*PIXEL_SIZE);
    }

    paintFocus(painter);
    painter.end();
}

void CharSetWidget::paintFocus(QPainter &painter)
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
                         0, ROWS * PIXEL_SIZE * 8);
        painter.drawLine(COLUMNS * PIXEL_SIZE * 8, 0,
                         COLUMNS * PIXEL_SIZE * 8, ROWS * PIXEL_SIZE * 8);

        // horizontal lines
        painter.drawLine(0, 0,
                         COLUMNS * PIXEL_SIZE * 8, 0);
        painter.drawLine(0, ROWS * PIXEL_SIZE * 8,
                         COLUMNS * PIXEL_SIZE * 8, ROWS * PIXEL_SIZE * 8);
    }
}

void CharSetWidget::setTileIndex(int tileIndex)
{
    auto state = State::getInstance();
    int t = state->getTileIndexFromCharIndex(_cursorPos.y() * COLUMNS + _cursorPos.x());

    // comparing tileIndex instead of charIndex is needed in order to allow
    // left, up and down navigation
    if (tileIndex != t) {
        int charIndex = state->getCharIndexFromTileIndex(tileIndex);
        _cursorPos.setX(charIndex % COLUMNS);
        _cursorPos.setY(charIndex / COLUMNS);
        update();
    }
}

bool CharSetWidget::hasSelection() const
{
    return (_selecting && _selectingSize.width()!=0 && _selectingSize.height()!=0);
}

void CharSetWidget::getSelectionRange(State::CopyRange* copyRange) const
{
    Q_ASSERT(copyRange);

    if (hasSelection())
    {
        // calculate absolute values of origin/size
        QPoint fixed_origin = _cursorPos;
        QSize fixed_size = _selectingSize;

        if (_selectingSize.width() < 0)
        {
            fixed_origin.setX(_cursorPos.x() + _selectingSize.width());
            fixed_size.setWidth(-_selectingSize.width());
        }

        if (_selectingSize.height() < 0)
        {
            fixed_origin.setY(_cursorPos.y() + _selectingSize.height());
            fixed_size.setHeight(-_selectingSize.height());
        }


        // transform origin/size to offset, blockSize, ...

        copyRange->offset = fixed_origin.y() * COLUMNS + fixed_origin.x();
        copyRange->blockSize = fixed_size.width();
        copyRange->count = fixed_size.height();
        copyRange->skip = COLUMNS - fixed_size.width();
    }
    else
    {
        auto state = State::getInstance();

        // No selection, so copy current tile
        int charIndex = _cursorPos.y() * COLUMNS + _cursorPos.x();
        int tileIndex = state->getTileIndexFromCharIndex(charIndex);

        auto tileProperties = state->getTileProperties();

        if (tileProperties.interleaved == 1)
        {
            copyRange->offset = tileIndex * tileProperties.size.width() * tileProperties.size.height();
            copyRange->blockSize = tileProperties.size.width() * tileProperties.size.height();
            copyRange->count = 1;
            copyRange->skip = 0;
        }
        else
        {
            copyRange->offset = tileIndex * tileProperties.interleaved;
            copyRange->blockSize = 1;
            copyRange->count = tileProperties.size.width() * tileProperties.size.height();
            copyRange->skip = tileProperties.interleaved-1;
        }
    }
}

int CharSetWidget::getCursorPos() const
{
    return _cursorPos.y() * COLUMNS + _cursorPos.x();
}
