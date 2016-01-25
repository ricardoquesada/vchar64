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
#include <QMdiSubWindow>

#include "bigcharwidget.h"
#include "palette.h"
#include "state.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "utils.h"

static const int COLUMNS = 32;
static const int ROWS = 8;
static const int OFFSET = 2;

CharsetWidget::CharsetWidget(QWidget *parent)
    : QWidget(parent)
    , _cursorPos({0,0})
    , _selecting(false)
    , _selectingSize({1,1})
    , _charIndex(-1)
    , _sizeHint({0,0})
    , _pixelSize({0,0})
{
    _sizeHint = {COLUMNS * 8 * 2,
                 ROWS * 8 * 2};
    setMinimumSize(_sizeHint);
}

void CharsetWidget::updateCharIndex(int charIndex)
{
    if (_charIndex != charIndex && charIndex >=0 && charIndex < 256)
    {
        _charIndex = charIndex;
        auto state = MainWindow::getCurrentState();
        if (state)
            state->setCharIndex(charIndex);
    }
}

//
// Overrides
//
void CharsetWidget::mousePressEvent(QMouseEvent * event)
{
    event->accept();

    auto pos = event->localPos();

    int x = (pos.x() - OFFSET) / _pixelSize.width() / 8;
    int y = (pos.y() - OFFSET) / _pixelSize.height() / 8;
    int charIndex = x + y * COLUMNS;

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
                qBound(-_cursorPos.x(), _selectingSize.width(), COLUMNS-_cursorPos.x()),
                qBound(-_cursorPos.y(), _selectingSize.height(), ROWS-_cursorPos.y())
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
            updateCharIndex(charIndex);
        }

        update();
    }
}

void CharsetWidget::mouseMoveEvent(QMouseEvent * event)
{
    event->accept();

    if (event->buttons() == Qt::LeftButton)
    {
        auto pos = event->localPos();
        int x = (pos.x() - OFFSET) / _pixelSize.width() / 8;
        int y = (pos.y() - OFFSET) / _pixelSize.height() / 8;

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

void CharsetWidget::keyPressEvent(QKeyEvent *event)
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

void CharsetWidget::paintEvent(QPaintEvent *event)
{
    auto state = MainWindow::getCurrentState();

    // no open documents?
    if (!state)
        return;

    QPainter painter;

    painter.begin(this);
    painter.fillRect(event->rect(), QWidget::palette().color(QWidget::backgroundRole()));

    painter.setBrush(QColor(0,0,0));
    painter.setPen(Qt::NoPen);

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

            utilsDrawChar(state, &painter, _pixelSize, QPoint(OFFSET, OFFSET), QPoint(w, h), index);
        }
    }

    if (_selecting) {
        pen.setColor({149,195,244,255});
        painter.setPen(pen);
        painter.setBrush(QColor(149,195,244,64));
        painter.drawRect(_cursorPos.x() * 8 * _pixelSize.width() + OFFSET,
                         _cursorPos.y() * 8 * _pixelSize.height() + OFFSET,
                         _selectingSize.width() * 8 * _pixelSize.width(),
                         _selectingSize.height() * 8 * _pixelSize.height());
    }
    else
    {
        pen.setColor({149,195,244,255});
        painter.setPen(pen);
        painter.setBrush(QColor(128,0,0,0));
        painter.drawRect(_cursorPos.x() * 8 * _pixelSize.width() + OFFSET,
                         _cursorPos.y() * 8 * _pixelSize.height() + OFFSET,
                         8 * _pixelSize.width(),
                         8 * _pixelSize.height());
    }

    paintFocus(painter);
    painter.end();
}

QSize CharsetWidget::sizeHint() const
{
    return _sizeHint;
}

void CharsetWidget::resizeEvent(QResizeEvent* event)
{
    Q_UNUSED(event);

    auto pixel_size_x = size().width() / (COLUMNS * 8);
    auto pixel_size_y = size().height() / (ROWS * 8);

    // keep aspect ratio
    auto pixel_size = qMin(pixel_size_x, pixel_size_y);
    _pixelSize = {pixel_size, pixel_size};
}

//
// Helpers
//
void CharsetWidget::paintFocus(QPainter &painter)
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
                         0, ROWS * _pixelSize.height() * 8 + OFFSET);
        painter.drawLine(COLUMNS * _pixelSize.width() * 8 + OFFSET, 0,
                         COLUMNS * _pixelSize.width() * 8 + OFFSET, ROWS * _pixelSize.height() * 8 + OFFSET);

        // horizontal lines
        painter.drawLine(0, 0,
                         COLUMNS * _pixelSize.width() * 8 + OFFSET, 0);
        painter.drawLine(0, ROWS * _pixelSize.height() * 8 + OFFSET,
                         COLUMNS * _pixelSize.width() * 8 + OFFSET, ROWS * _pixelSize.height() * 8 + OFFSET);
    }
}

//
// slots
//
void CharsetWidget::onCharIndexUpdated(int charIndex)
{
    // if the charIndex is updated, cancel selection
    if (_selecting)
    {
        _selecting = false;
        _selectingSize = {1,1};
    }

    _cursorPos.setX(charIndex % COLUMNS);
    _cursorPos.setY(charIndex / COLUMNS);
    update();
}

void CharsetWidget::onMulticolorModeToggled(bool state)
{
    Q_UNUSED(state);
    update();
}

void CharsetWidget::updateColor()
{
    update();
}

//
// public
//
bool CharsetWidget::hasSelection() const
{
    return (_selecting && _selectingSize.width()!=0 && _selectingSize.height()!=0);
}

void CharsetWidget::getSelectionRange(State::CopyRange* copyRange) const
{
    Q_ASSERT(copyRange);

    // if charset has no selection, it works the same
    // since selectingSize will be {1,1}

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

    copyRange->type = State::CopyRange::CHARS;
    copyRange->tileProperties.size = {-1, -1};
    copyRange->tileProperties.interleaved = -1;
}

int CharsetWidget::getCursorPos() const
{
    return _cursorPos.y() * COLUMNS + _cursorPos.x();
}
