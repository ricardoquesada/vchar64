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

#include "mapwidget.h"

#include <functional>

#include <QPainter>
#include <QPaintEvent>
#include <QFile>
#include <QDebug>
#include <QGuiApplication>
#include <QImage>

#include "palette.h"
#include "state.h"
#include "mainwindow.h"
#include "utils.h"

static const int ZOOM_LEVEL = 2;
static const int OFFSET = 0;

MapWidget::MapWidget(QWidget *parent)
    : QWidget(parent)
    , _displayGrid(false)
    , _selecting(false)
    , _selectingSize({1,1})
    , _cursorPos({0,0})
    , _mapSize({40,25})
    , _tileSize({1,1})
    , _mode(SELECT_MODE)
    , _commandMergeable(false)
    , _zoomLevel(ZOOM_LEVEL)
    , _altValue(-1)
{
    // FIXME: should be updated when the map size changes
    _sizeHint = {(int)(_mapSize.width() * _tileSize.width() * _zoomLevel * 8),
                 (int)(_mapSize.height() * _tileSize.height() * _zoomLevel * 8)};
    setMinimumSize(_sizeHint);

    setMouseTracking(true);

    for (int i=0;i < 256; ++i)
        _tileImages[i] = nullptr;
}

//
// Overriden
//
void MapWidget::paintEvent(QPaintEvent *event)
{
    auto state = MainWindow::getCurrentState();

    // no open documents?
    if (!state)
        return;

    updateTileImages();

    auto mapSize = state->getMapSize();

    // FIXME:
    _mapSize = mapSize;

    QPainter painter;
    painter.begin(this);
    painter.scale(_zoomLevel, _zoomLevel);

    painter.fillRect(event->rect(), QWidget::palette().color(QWidget::backgroundRole()));

    painter.setBrush(QColor(0,0,0));
    painter.setPen(Qt::NoPen);

    auto tileProperties = state->getTileProperties();
    _tileSize = tileProperties.size;
    const int tw = _tileSize.width();
    const int th = _tileSize.height();

    for (int y=0; y<mapSize.height(); y++)
    {
        for (int x=0; x<mapSize.width(); ++x)
        {
            auto tileIdx = state->getTileIndexFromMap(QPoint(x,y));
            QRectF target(x * _tileSize.width() * 8, y * _tileSize.height() * 8,
                          _tileSize.width() * 8, _tileSize.height() * 8);
            painter.drawImage(target, *_tileImages[tileIdx], _tileImages[tileIdx]->rect());
        }
    }

    if (_displayGrid)
    {
        auto pen = painter.pen();
        pen.setColor(QColor(0,128,0));
        pen.setStyle(Qt::DashLine);
        pen.setWidthF(1 / ZOOM_LEVEL);
        painter.setPen(pen);

        for (int y=0; y<=mapSize.height(); ++y)
            painter.drawLine(QPointF(0, y * th * 8),
                             QPointF(mapSize.width() * tw * 8, y * th * 8));

        for (int x=0; x<=mapSize.width(); ++x)
            painter.drawLine(QPointF(x * tw * 8, 0),
                             QPointF(x * tw * 8, mapSize.height() * th *8));
    }

    QPen pen;
    pen.setColor({149,195,244,255});
    if (hasFocus())
        pen.setWidthF(3 / ZOOM_LEVEL);
    else
        pen.setWidthF(1 / ZOOM_LEVEL);
    pen.setStyle(Qt::PenStyle::SolidLine);

    if (_selecting) {
        int plusOneX = _selectingSize.width() < 0 ? 1 : 0;
        int plusOneY = _selectingSize.height() < 0 ? 1 : 0;

        pen.setColor({149,195,244,255});
        painter.setPen(pen);
        painter.setBrush(QColor(149,195,244,64));
        painter.drawRect((_cursorPos.x() + plusOneX) * 8 * tw + OFFSET,
                         (_cursorPos.y() + plusOneY) * 8 * th + OFFSET,
                         (_selectingSize.width() - plusOneX) * tw * 8,
                         (_selectingSize.height() - plusOneY) * th * 8);
    }
    else
    {
        pen.setColor({149,195,244,255});
        painter.setPen(pen);
        painter.setBrush(QColor(128,0,0,0));
        painter.drawRect(_cursorPos.x() * 8 * tw + OFFSET,
                         _cursorPos.y() * 8 * th + OFFSET,
                         8 * tw, 8 * th);
    }

    painter.end();
}

void MapWidget::mousePressEvent(QMouseEvent * event)
{
    event->accept();

    auto state = MainWindow::getCurrentState();
    if (!state)
        return;

    auto pos = event->localPos();

    int x = (pos.x() - OFFSET) / _zoomLevel / _tileSize.width() / 8;
    int y = (pos.y() - OFFSET) / _zoomLevel / _tileSize.height() / 8;

    // sanity check
    x = qBound(0, x, _mapSize.width() - 1);
    y = qBound(0, y, _mapSize.height() - 1);

    if (event->button() == Qt::LeftButton)
    {
        if (_mode == SELECT_MODE)
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
                    qBound(-_cursorPos.x(), _selectingSize.width(), _mapSize.width() - _cursorPos.x()),
                    qBound(-_cursorPos.y(), _selectingSize.height(), _mapSize.height() - _cursorPos.y())
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

                // update tileIndex
                state->setTileIndex(state->getTileIndexFromMap(_cursorPos));
            }

            update();
        }
        else if (_mode == PAINT_MODE)
        {
            _cursorPos = {x,y};
            auto state = MainWindow::getCurrentState();
            if (state) {
                state->mapPaint(QPoint(x,y), state->getTileIndex(), _commandMergeable);
                _commandMergeable = true;
            }
        }
        else if (_mode == FILL_MODE)
        {
            _cursorPos = {x,y};
            auto state = MainWindow::getCurrentState();
            if (state)
                state->mapFill(QPoint(x,y), state->getTileIndex());
        }
    }
}

void MapWidget::mouseMoveEvent(QMouseEvent * event)
{
    event->accept();

    auto pos = event->localPos();
    int x = (pos.x() - OFFSET) / _zoomLevel / _tileSize.width() / 8;
    int y = (pos.y() - OFFSET) / _zoomLevel / _tileSize.height() / 8;

    x = qBound(0, x, _mapSize.width()-1);
    y = qBound(0, y, _mapSize.height()-1);

    if (event->buttons() == Qt::NoButton)
    {
        MainWindow::getInstance()->showMessageOnStatusBar(tr("x: %1, y: %2").arg(x).arg(y));
    }
    else if (event->buttons() == Qt::LeftButton)
    {

        if (_mode == SELECT_MODE)
        {
            if (x >= _cursorPos.x())
                x++;
            if (y >= _cursorPos.y())
                y++;

            _selectingSize = {x - _cursorPos.x(),
                              y - _cursorPos.y()};

            // sanity check
            _selectingSize = {
                qBound(-_cursorPos.x(), _selectingSize.width(), _mapSize.width()-_cursorPos.x()),
                qBound(-_cursorPos.y(), _selectingSize.height(), _mapSize.height()-_cursorPos.y())
            };

            _selecting = true;
            update();
        }
        else if (_mode == PAINT_MODE)
        {
            _cursorPos = {x,y};
            auto state = MainWindow::getCurrentState();
            if (state) {
                state->mapPaint(QPoint(x,y), state->getTileIndex(), _commandMergeable);
                _commandMergeable = true;
            }
        }
    }
}

void MapWidget::mouseReleaseEvent(QMouseEvent * event)
{
    event->accept();

    // don't merge "mapPaint" command if the mouse was released
    _commandMergeable = false;
}

void MapWidget::keyPressEvent(QKeyEvent *event)
{
    event->accept();

    auto state = MainWindow::getCurrentState();
    if (!state)
        return;

    auto oldCursorPos = _cursorPos;
    auto oldSelecting = _selecting;
    auto oldSelectingSize = _selectingSize;
    bool spacePressed = false;
    bool typing = false;

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

    case Qt::Key_Backspace:
    case Qt::Key_Delete:
        state->mapPaint(_cursorPos, 0x20, false);
        point = {-1,0};
        break;

    case Qt::Key_Enter:
    case Qt::Key_Return:
        point = {-_cursorPos.x(),+1};
        break;

    case Qt::Key_Space:
        spacePressed = true;
        if (_mode != SELECT_MODE)
            break;
        /* else fall-through */
    default:
        if (event->text().isEmpty())
        {
            QWidget::keyPressEvent(event);
            return;
        }

        // Alting (ALT + number) ?
        if (event->modifiers() & Qt::AltModifier &&
                event->key() >= Qt::Key_0 &&
                event->key() <= Qt::Key_9)
        {
            // 0-based
            int keyValue = event->key() - Qt::Key_0;
            if (_altValue == -1)
                _altValue = keyValue;
            else
                _altValue = _altValue * 10 + keyValue;
        }
        else
        {
            auto asciiCode = event->text().toLatin1()[0];
            auto screenCode = utilsAsciiToScreenCode(asciiCode);
            state->mapPaint(_cursorPos, screenCode, false);
            point = {+1,0};
            typing = true;
        }
        break;
    }

    bool selecting = false;

    if (_mode == SELECT_MODE)
        selecting = (event->modifiers() & Qt::ShiftModifier) && !typing;

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
                qBound(-_cursorPos.x(), _selectingSize.width(), _mapSize.width()-_cursorPos.x()),
                qBound(-_cursorPos.y(), _selectingSize.height(), _mapSize.height()-_cursorPos.y())
            };
        }
        else
        {
            _cursorPos += point;
            _cursorPos = {qBound(0, _cursorPos.x(), _mapSize.width()-1),
                          qBound(0, _cursorPos.y(), _mapSize.height()-1)};

            MainWindow::getInstance()->showMessageOnStatusBar(tr("x: %1, y: %2")
                                                              .arg(_cursorPos.x())
                                                              .arg(_cursorPos.y()));
        }
    }
    _selecting = selecting;

    // update tile index
    if (_mode == SELECT_MODE && _cursorPos != oldCursorPos)
    {
        state->setTileIndex(state->getTileIndexFromMap(_cursorPos));
    }

    // update cursor or select range
    if (_cursorPos != oldCursorPos || _selecting != oldSelecting || _selectingSize != oldSelectingSize)
    {
        update();
    }

    if (_mode == PAINT_MODE && spacePressed)
    {
        state->mapPaint(_cursorPos, state->getTileIndex(), false);
    }
    else if (_mode == FILL_MODE && spacePressed)
    {
        state->mapFill(_cursorPos, state->getTileIndex());
    }
}

void MapWidget::keyReleaseEvent(QKeyEvent *event)
{
    event->accept();

    if (_altValue != -1 && !(event->modifiers() & Qt::AltModifier))
    {
        auto state = MainWindow::getCurrentState();
        state->mapPaint(_cursorPos, _altValue, false);
        _cursorPos += {+1, 0};
        _cursorPos = {qBound(0, _cursorPos.x(), _mapSize.width()-1),
                      qBound(0, _cursorPos.y(), _mapSize.height()-1)};
        _altValue = -1;
    }
}

QSize MapWidget::sizeHint() const
{
    return _sizeHint;
}

//
//
//
void MapWidget::getSelectionRange(State::CopyRange* copyRange) const
{
    Q_ASSERT(copyRange);

    // if map has no selection, it works the same
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

    // transform origin/size to offset, blockSize, ...

    copyRange->offset = fixed_origin.y() * _mapSize.width() + fixed_origin.x();
    copyRange->blockSize = fixed_size.width();
    copyRange->count = fixed_size.height();
    copyRange->skip = _mapSize.width() - fixed_size.width();

    copyRange->type = State::CopyRange::MAP;
    copyRange->tileProperties.size = {-1, -1};
    copyRange->tileProperties.interleaved = -1;
}

int MapWidget::getCursorPos() const
{
    return _cursorPos.y() * _mapSize.width() + _cursorPos.x();
}

void MapWidget::enableGrid(bool enabled)
{
    if (_displayGrid != enabled)
    {
        _displayGrid = enabled;
        update();
    }
}

void MapWidget::setZoomLevel(int zoomLevel)
{
    _zoomLevel = zoomLevel * ZOOM_LEVEL / 100.0;
    onMapSizeUpdated();
}

//
// slots
//

void MapWidget::onTilePropertiesUpdated()
{
    _tileSize = MainWindow::getCurrentState()->getTileProperties().size;

    _sizeHint = QSize(_mapSize.width() * _tileSize.width() * _zoomLevel * 8,
                      _mapSize.height() * _tileSize.height() * _zoomLevel * 8);

    setMinimumSize(_sizeHint);
    update();
}

void MapWidget::onMapSizeUpdated()
{
    _mapSize = MainWindow::getCurrentState()->getMapSize();

    _sizeHint = QSize(_mapSize.width() * _tileSize.width() * _zoomLevel * 8,
                      _mapSize.height() * _tileSize.height() * _zoomLevel * 8);

    setMinimumSize(_sizeHint);
    update();
}

void MapWidget::onMapContentUpdated()
{
    update();
}

void MapWidget::onMulticolorModeToggled(bool state)
{
    Q_UNUSED(state);
    update();
}

void MapWidget::onColorPropertiesUpdated(int pen)
{
    Q_UNUSED(pen);
    update();
}

void MapWidget::onTileUpdated(int tileIndex)
{
    Q_UNUSED(tileIndex);
    update();
}

void MapWidget::onCharsetUpdated()
{
    update();
}

void MapWidget::setMode(MapMode mode)
{
    if (_mode != mode)
        _mode = mode;
}

void MapWidget::onFileLoaded()
{
    onTilePropertiesUpdated();
    onMapSizeUpdated();
}

void MapWidget::updateTileImages()
{
    auto state = MainWindow::getCurrentState();
    if (!state)
        return;

    auto properties = state->getTileProperties();
    const int totalTiles = 256 / (properties.size.width() * properties.size.height());

    // resize tiles
    for (int i=0; i<totalTiles; ++i)
    {
        if (!_tileImages[i] || _tileImages[i]->size() != properties.size)
        {
            if (_tileImages[i])
                delete _tileImages[i];
            _tileImages[i] = new QImage(properties.size * 8, QImage::Format_RGB888);
        }
    }

    const auto tileProperties = state->getTileProperties();
    const int tw = tileProperties.size.width();
    const int th = tileProperties.size.height();

    for (int tileIdx=0; tileIdx<totalTiles; ++tileIdx)
    {
        quint8 charIdx = tileProperties.interleaved == 1 ?
                                                    tileIdx * tw * th :
                                                    tileIdx;

        for (int char_quadrant=0; char_quadrant < (tw * th); char_quadrant++)
        {
            int offset_x = (char_quadrant % tw) * 8;
            int offset_y = (char_quadrant / tw) * 8;

            utilsDrawCharInImage(state, _tileImages[tileIdx], QPoint(offset_x,offset_y), charIdx);

            charIdx += tileProperties.interleaved;
        }
    }
}
