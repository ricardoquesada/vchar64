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

#include "tilesetwidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QtMath>
#include <QDebug>

#include "preferences.h"
#include "palette.h"
#include "state.h"
#include "mainwindow.h"
#include "utils.h"
#include "ui_mainwindow.h"

static const int COLUMNS = 32;
static const int ROWS = 8;
static const int OFFSET = 2;
static const int ZOOM_LEVEL = 2;

TilesetWidget::TilesetWidget(QWidget *parent)
    : QWidget(parent)
    , _cursorPos({0,0})
    , _selecting(false)
    , _selectingSize({0,0})
    , _columns(COLUMNS)
    , _tileColumns(COLUMNS)
    , _rows(ROWS)
    , _tileRows(ROWS)
    , _maxTiles(256)
    , _sizeHint({0,0})
    , _zoomLevel(ZOOM_LEVEL)
    , _displayGrid(false)
{
    _sizeHint = {(_columns * 8 + OFFSET) * ZOOM_LEVEL + 1,
                 (_rows * 8 + OFFSET) * ZOOM_LEVEL + 1};
    setMinimumSize(_sizeHint);

    setMouseTracking(true);
}

//
// overrides
//
void TilesetWidget::mousePressEvent(QMouseEvent * event)
{
    event->accept();

    auto state = MainWindow::getCurrentState();
    if (!state)
        return;

    if (event->button() == Qt::LeftButton)
    {

        auto pos = event->localPos();
        auto tileProperties = state->getTileProperties();
        int tw = tileProperties.size.width();
        int th = tileProperties.size.height();

        int x = (pos.x() / _zoomLevel - OFFSET) / 8 / tw;
        int y = (pos.y() /_zoomLevel - OFFSET) / 8 / th;


        // sanity check
        x = qBound(0, x, _tileColumns - 1);
        y = qBound(0, y, _tileRows - 1);

        int tileIndex = x + y * (_columns / tw);

        // quick sanity check
        if (! (tileIndex >= 0 && tileIndex < _maxTiles))
            return;

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
                qBound(-_cursorPos.x(), _selectingSize.width(), _tileColumns - _cursorPos.x()),
                qBound(-_cursorPos.y(), _selectingSize.height(), _tileRows - _cursorPos.y())
            };
            update();
        }
        else
        {
            // different and valid tileIndex?
            if (_cursorPos.x() != x || _cursorPos.y() != y)
            {
                _selecting = false;
                _selectingSize = {1,1};

                _cursorPos = {x,y};
                state->setTileIndex(tileIndex);
                update();
            }
        }
    }
}

void TilesetWidget::mouseMoveEvent(QMouseEvent * event)
{
    event->accept();

    auto pos = event->localPos();
    auto state = MainWindow::getCurrentState();
    auto tileProperties = state->getTileProperties();
    int tw = tileProperties.size.width();
    int th = tileProperties.size.height();

    int x = (pos.x() / _zoomLevel - OFFSET) / 8 / tw;
    int y = (pos.y() /_zoomLevel - OFFSET) / 8 / th;

    x = qBound(0, x, _tileColumns-1);
    y = qBound(0, y, _tileRows-1);

    if (event->buttons() == Qt::NoButton)
    {
        MainWindow::getInstance()->showMessageOnStatusBar(tr("x: %1, y: %2").arg(x).arg(y));
    }
    else if (event->buttons() == Qt::LeftButton)
    {
        // sanity check
        int tileIndex = x + y * (_columns / tw);
        if (! (tileIndex >= 0 && tileIndex < _maxTiles))
            return;

        if (x >= _cursorPos.x())
            x++;
        if (y >= _cursorPos.y())
            y++;

        _selectingSize = {x - _cursorPos.x(),
                          y - _cursorPos.y()};

        // sanity check
        _selectingSize = {
            qBound(-_cursorPos.x(), _selectingSize.width(), _tileColumns-_cursorPos.x()),
            qBound(-_cursorPos.y(), _selectingSize.height(), _tileRows-_cursorPos.y())
        };

        _selecting = true;

        update();
    }
}

void TilesetWidget::keyPressEvent(QKeyEvent *event)
{
    event->accept();

    auto state = MainWindow::getCurrentState();
    if (!state)
        return;

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
                qBound(-_cursorPos.x(), _selectingSize.width(), _tileColumns-_cursorPos.x()),
                qBound(-_cursorPos.y(), _selectingSize.height(), _tileRows-_cursorPos.y())
            };
        }
        else
        {
            auto pos = _cursorPos + point;
            pos = {qBound(0, pos.x(), _tileColumns-1),
                   qBound(0, pos.y(), _tileRows-1)};

            int tileIdx =  pos.y() * _tileColumns + pos.x();
            if (pos != _cursorPos && tileIdx >=0 && tileIdx < _maxTiles)
            {
                _cursorPos = pos;
                state->setTileIndex(tileIdx);

                MainWindow::getInstance()->showMessageOnStatusBar(tr("x: %1, y: %2")
                                                                  .arg(_cursorPos.x())
                                                                  .arg(_cursorPos.y()));
            }
        }
    }

    _selecting = selecting;
    update();
}

void TilesetWidget::paintEvent(QPaintEvent *event)
{
    auto state = MainWindow::getCurrentState();

    // no open documents?
    if (!state)
        return;

    QPainter painter;

    painter.begin(this);
    painter.scale(_zoomLevel, _zoomLevel);
    painter.fillRect(event->rect(), QWidget::palette().color(QWidget::backgroundRole()));

    painter.setBrush(QColor(0,0,0));
    painter.setPen(Qt::NoPen);

    QPen pen;
    pen.setColor({149,195,244,255});
    pen.setWidthF((hasFocus() ? 3 : 1) / _zoomLevel );
    pen.setStyle(Qt::PenStyle::SolidLine);

    auto tileProperties = state->getTileProperties();
    int tw = tileProperties.size.width();
    int th = tileProperties.size.height();

    int max_tiles = 256 / (tw*th);

    for (int i=0; i<max_tiles;i++)
    {
        quint8 charIdx = tileProperties.interleaved == 1 ?
                                                    i * tw * th :
                                                    i;

        int w = (i * tw) % _columns;
        int h = th * ((i * tw) / _columns);

        for (int char_idx=0; char_idx < (tw * th); char_idx++)
        {
            int local_w = w + char_idx % tw;
            int local_h = h + char_idx / tw;

            utilsDrawCharInPainter(state, &painter, QSizeF(1, 1), QPoint(OFFSET, OFFSET), QPoint(local_w, local_h), charIdx);

            charIdx += tileProperties.interleaved;
        }
    }

    if (_displayGrid)
    {
        auto pen = painter.pen();
        pen.setColor(Preferences::getInstance().getGridColor());
        pen.setStyle(Qt::DashLine);
        pen.setWidthF(1.0 / _zoomLevel);
        painter.setPen(pen);

        for (int y=0; y <= _tileRows; ++y)
            painter.drawLine(QPointF(0 + OFFSET, y * 8 * th + OFFSET),
                             QPointF(_tileColumns * 8 * tw + OFFSET, y * 8 * th + OFFSET));

        for (int x=0; x <= _tileColumns; ++x)
            painter.drawLine(QPointF(x * 8 * tw + OFFSET, OFFSET),
                             QPointF(x * 8 * tw + OFFSET, _tileRows * 8 * th + OFFSET));
    }

    painter.setPen(Qt::NoPen);

    paintFocus(painter);
    paintSelectedTile(painter);

    painter.end();
}

QSize TilesetWidget::sizeHint() const
{
    return _sizeHint;
}

//
// helpers
//
void TilesetWidget::paintSelectedTile(QPainter& painter)
{
    auto tileProperties = MainWindow::getCurrentState()->getTileProperties();
    int tw = tileProperties.size.width();
    int th = tileProperties.size.height();

    QPen pen;
    pen.setWidthF(2 / _zoomLevel);
    pen.setColor({149,195,244,255});
    pen.setStyle(Qt::PenStyle::SolidLine);

    if (_selecting)
    {
        int plusOneX = _selectingSize.width() < 0 ? 1 : 0;
        int plusOneY = _selectingSize.height() < 0 ? 1 : 0;

        painter.setPen(pen);
        painter.setBrush(QColor(149,195,244,64));
        painter.drawRect((_cursorPos.x() + plusOneX) * 8 * tw + OFFSET,
                         (_cursorPos.y() + plusOneY) * 8 * th + OFFSET,
                         (_selectingSize.width() - plusOneX) * 8 * tw,
                         (_selectingSize.height() - plusOneY) * 8 * th);
    }
    else
    {
        painter.setPen(pen);
        painter.setBrush(QColor(128,0,0,0));
        painter.drawRect(_cursorPos.x() * 8 * tw + OFFSET,
                         _cursorPos.y() * 8 * th + OFFSET,
                         8 * tw,
                         8 * th);
    }
}

void TilesetWidget::paintFocus(QPainter &painter)
{
    if (hasFocus())
    {
        QPen pen;
        pen.setColor({149,195,244,255});
        pen.setWidthF(3 / _zoomLevel);
        pen.setStyle(Qt::PenStyle::SolidLine);

        painter.setPen(pen);

        // vertical lines
        painter.drawLine(OFFSET-1, OFFSET-1,
                         OFFSET-1, _rows * 8 + OFFSET);
        painter.drawLine(_columns * 8 + OFFSET, OFFSET-1,
                         _columns * 8 + OFFSET, _rows * 8 + OFFSET);

        // horizontal lines
        painter.drawLine(OFFSET-1, OFFSET-1,
                         _columns * 8 + 1, OFFSET-1);
        painter.drawLine(OFFSET-1, _rows * 8 + OFFSET,
                         _columns * 8 + OFFSET, _rows * 8 + OFFSET);
    }
}

//
// SLOTS
//
void TilesetWidget::onTileIndexUpdated(int selectedTileIndex)
{
    // if the tileIndex is updated, cancel selection
    if (_selecting)
    {
        _selecting = false;
        _selectingSize = {1,1};
    }

    int x = selectedTileIndex % _tileColumns;
    int y = selectedTileIndex / _tileColumns;
    if (_cursorPos.x() != x || _cursorPos.y() != y)
    {
        _cursorPos = {x, y};
        update();
    }
}

void TilesetWidget::onTilePropertiesUpdated()
{
    auto state = MainWindow::getCurrentState();
    auto properties = state->getTileProperties();

    _columns = (COLUMNS / properties.size.width()) * properties.size.width();
    _tileColumns = _columns / properties.size.width();

    _rows = qCeil((256.0f / _columns) / properties.size.height()) * properties.size.height();
    _tileRows = _rows / properties.size.height();

    _maxTiles = 256 / (properties.size.width() * properties.size.height());

    _sizeHint = QSize(_zoomLevel * _columns * 8 + OFFSET * 2,
                 _zoomLevel * _rows * 8 + OFFSET * 2);

    setMinimumSize(_sizeHint);
    update();
}

void TilesetWidget::onMulticolorModeToggled(bool state)
{
    Q_UNUSED(state);
    update();
}

void TilesetWidget::onColorPropertiesUpdated(int pen)
{
    Q_UNUSED(pen);
    update();
}

void TilesetWidget::onTileUpdated(int tileIndex)
{
    Q_UNUSED(tileIndex);
    update();
}

void TilesetWidget::onCharsetUpdated()
{
    update();
}

void TilesetWidget::onFileLoaded()
{
    onTilePropertiesUpdated();
}

// public
bool TilesetWidget::hasSelection() const
{
    return (_selecting && _selectingSize.width() != 0 && _selectingSize.height() != 0);
}

void TilesetWidget::getSelectionRange(State::CopyRange* copyRange) const
{
    Q_ASSERT(copyRange);

    auto tileProperties = MainWindow::getCurrentState()->getTileProperties();

    // if charset has no selection, it works the same
    // since selectingSize will be {1,1}

    // calculate absolute values of origin/size
    QPoint fixed_origin = _cursorPos;
    QSize fixed_size = _selectingSize;

    if (_selectingSize.width() < 0)
    {
        fixed_origin.setX(_cursorPos.x() + _selectingSize.width());
        fixed_size.setWidth(-_selectingSize.width() + 1);
    }

    if (_selectingSize.height() < 0)
    {
        fixed_origin.setY(_cursorPos.y() + _selectingSize.height());
        fixed_size.setHeight(-_selectingSize.height() + 1);
    }

    // copying tiles, intead of chars, even if interleaved==1
    copyRange->offset = fixed_origin.y() * _tileColumns + fixed_origin.x();
    copyRange->blockSize = fixed_size.width();
    copyRange->count = fixed_size.height();
    copyRange->skip = _tileColumns - fixed_size.width();

    copyRange->type = State::CopyRange::TILES;
    copyRange->tileProperties = tileProperties;

    copyRange->bufferSize = State::CHAR_BUFFER_SIZE + State::TILE_COLORS_BUFFER_SIZE;
}

void TilesetWidget::enableGrid(bool enabled)
{
    if (_displayGrid != enabled)
    {
        _displayGrid = enabled;
        update();
    }
}

void TilesetWidget::setZoomLevel(int zoomLevel)
{
    _zoomLevel = zoomLevel * ZOOM_LEVEL / 100.0;

    _sizeHint = QSize((COLUMNS * 8 + OFFSET) * _zoomLevel + 1,
                      (ROWS * 8 + OFFSET) * _zoomLevel + 1);

    setMinimumSize(_sizeHint);
    update();
}
