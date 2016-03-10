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
#include <QPointF>

#include "bigcharwidget.h"
#include "palette.h"
#include "state.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "utils.h"

static const int COLUMNS = 32;
static const int ROWS = 8;
static const int OFFSET = 2;
static const int PIXEL_SIZE = 2;

CharsetWidget::CharsetWidget(QWidget *parent)
    : QWidget(parent)
    , _cursorPos({0,0})
    , _selecting(false)
    , _selectingSize({1,1})
    , _charIndex(-1)
    , _sizeHint({0,0})
    , _pixelSize(PIXEL_SIZE)
    , _displayGrid(false)
{
    _sizeHint = {(int)(COLUMNS * 8 * _pixelSize),
                 (int)(ROWS * 8 * _pixelSize)};
    setMinimumSize(_sizeHint);

    setMouseTracking(true);
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

    int x = (pos.x() - OFFSET) / _pixelSize / 8;
    int y = (pos.y() - OFFSET) / _pixelSize / 8;
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

    auto pos = event->localPos();
    int x = (pos.x() - OFFSET) / _pixelSize / 8;
    int y = (pos.y() - OFFSET) / _pixelSize / 8;
    x = qBound(0, x, COLUMNS-1);
    y = qBound(0, y, ROWS-1);

    if (event->buttons() == Qt::NoButton)
    {
        MainWindow::getInstance()->showMessageOnStatusBar(tr("x: %1, y: %2").arg(x).arg(y));
    }
    else if (event->buttons() == Qt::LeftButton)
    {
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

            MainWindow::getInstance()->showMessageOnStatusBar(tr("x: %1, y: %2")
                                                              .arg(_cursorPos.x())
                                                              .arg(_cursorPos.y()));
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

            utilsDrawChar(state, &painter, QSizeF(_pixelSize, _pixelSize), QPoint(OFFSET, OFFSET), QPoint(w, h), index);
        }
    }

    if (_displayGrid)
    {
        auto pen = painter.pen();
        pen.setColor(QColor(0,128,0));
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);

        for (int y=0; y <= ROWS; ++y)
            painter.drawLine(QPointF(0 + OFFSET, y * _pixelSize * 8 + OFFSET),
                             QPointF(COLUMNS * _pixelSize * 8 + OFFSET, y * _pixelSize * 8 + OFFSET));

        for (int x=0; x <= COLUMNS; ++x)
            painter.drawLine(QPointF(x * _pixelSize * 8 + OFFSET, 0),
                             QPointF(x * _pixelSize * 8 + OFFSET, ROWS * _pixelSize * 8 + OFFSET));
    }

    if (_selecting) {
        pen.setColor({149,195,244,255});
        painter.setPen(pen);
        painter.setBrush(QColor(149,195,244,64));
        painter.drawRect(_cursorPos.x() * 8 * _pixelSize + OFFSET,
                         _cursorPos.y() * 8 * _pixelSize + OFFSET,
                         _selectingSize.width() * 8 * _pixelSize,
                         _selectingSize.height() * 8 * _pixelSize);
    }
    else
    {
        pen.setColor({149,195,244,255});
        painter.setPen(pen);
        painter.setBrush(QColor(128,0,0,0));
        painter.drawRect(_cursorPos.x() * 8 * _pixelSize + OFFSET,
                         _cursorPos.y() * 8 * _pixelSize + OFFSET,
                         8 * _pixelSize,
                         8 * _pixelSize);
    }

    paintFocus(painter);
    painter.end();
}

QSize CharsetWidget::sizeHint() const
{
    return _sizeHint;
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
                         0, ROWS * _pixelSize * 8 + OFFSET);
        painter.drawLine(COLUMNS * _pixelSize * 8 + OFFSET, 0,
                         COLUMNS * _pixelSize * 8 + OFFSET, ROWS * _pixelSize * 8 + OFFSET);

        // horizontal lines
        painter.drawLine(0, 0,
                         COLUMNS * _pixelSize * 8 + OFFSET, 0);
        painter.drawLine(0, ROWS * _pixelSize * 8 + OFFSET,
                         COLUMNS * _pixelSize * 8 + OFFSET, ROWS * _pixelSize * 8 + OFFSET);
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

void CharsetWidget::onColorPropertiesUpdated(int pen)
{
    Q_UNUSED(pen);
    update();
}

void CharsetWidget::onTileUpdated(int tileIndex)
{
    Q_UNUSED(tileIndex);
    update();
}

void CharsetWidget::onCharsetUpdated()
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

void CharsetWidget::enableGrid(bool enabled)
{
    if (_displayGrid != enabled)
    {
        _displayGrid = enabled;
        update();
    }
}

void CharsetWidget::setZoomLevel(int zoomLevel)
{
    _pixelSize = zoomLevel * PIXEL_SIZE / 100.0;

    _sizeHint = QSize(COLUMNS * _pixelSize * 8, ROWS * _pixelSize * 8);

    setMinimumSize(_sizeHint);
    update();

}
