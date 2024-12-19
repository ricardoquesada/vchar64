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

#include "bigcharwidget.h"

#include <algorithm>

#include <QDebug>
#include <QMessageBox>
#include <QPaintEvent>
#include <QPainter>

#include "mainwindow.h"
#include "palette.h"
#include "state.h"
#include "ui_mainwindow.h"
#include "utils.h"

BigCharWidget::BigCharWidget(State* state, QWidget* parent)
    : QWidget(parent)
    , _tileIndex(0)
    , _charIndex(0)
    , _cursorPos({ 0, 0 })
    , _tileProperties({ { 1, 1 }, 1 })
    , _state(state)
    , _pixelSize({ 32, 32 })
    , _commandMergeable(false)
{
    Q_ASSERT(state && "Invalid State");
    _state->_bigCharWidget = this;

    setFocusPolicy(Qt::StrongFocus);

    setAttribute(Qt::WA_DeleteOnClose);

    setMouseTracking(true);
}

BigCharWidget::~BigCharWidget()
{
    delete _state;
}

int BigCharWidget::getTileIndex() const
{
    return _tileIndex;
}

QSize BigCharWidget::getTileSize() const
{
    return _tileProperties.size;
}

State* BigCharWidget::getState() const
{
    return _state;
}

void BigCharWidget::paintPixel(int x, int y, int pen)
{
    if (_state->tileGetPen(_tileIndex, QPoint(x, y)) != pen) {
        if (!_state->shouldBeDisplayedInMulticolor2(_tileIndex) && pen)
            pen = 1;
        _state->tilePaint(_tileIndex, QPoint(x, y), pen, _commandMergeable);
    }

    // redraw cursor
    update();
}

void BigCharWidget::cyclePixel(int x, int y)
{
    int pen = _state->tileGetPen(_tileIndex, QPoint(x, y));

    constexpr int nextPenMC[] = { State::PEN_FOREGROUND,
        State::PEN_MULTICOLOR2,
        State::PEN_BACKGROUND,
        State::PEN_MULTICOLOR1 };

    constexpr int nextPenHR[] = { State::PEN_FOREGROUND,
        State::PEN_BACKGROUND };

    if (_state->shouldBeDisplayedInMulticolor2(_tileIndex))
        pen = nextPenMC[pen];
    else
        pen = nextPenHR[pen];

    paintPixel(x, y, pen);
}

void BigCharWidget::mousePressEvent(QMouseEvent* event)
{
    event->accept();

    const auto pos = event->pos();

    const int x = int(pos.x()) / _pixelSize.width();
    const int y = int(pos.y()) / _pixelSize.height();
    if (x >= 8 * _tileProperties.size.width() || y >= 8 * _tileProperties.size.height())
        return;

    _cursorPos = { x, y };

    int selectedPen = _state->getSelectedPen();

    if (event->button() == Qt::LeftButton)
        paintPixel(x, y, selectedPen);
    else if (event->button() == Qt::RightButton)
        paintPixel(x, y, State::PEN_BACKGROUND);

    _commandMergeable = true;
}

void BigCharWidget::mouseMoveEvent(QMouseEvent* event)
{
    event->accept();

    const auto pos = event->pos();

    int x = int(pos.x()) / _pixelSize.width();
    int y = int(pos.y()) / _pixelSize.height();

    x = qBound(0, x, 8 * _tileProperties.size.width() - 1);
    y = qBound(0, y, 8 * _tileProperties.size.height() - 1);

    if (event->buttons() == Qt::NoButton) {
        MainWindow::getInstance()->showMessageOnStatusBar(tr("x: %1, y: %2").arg(x).arg(y));
    } else {
        _cursorPos = { x, y };
        int pen = _state->getSelectedPen();

        auto&& button = event->buttons();
        if (button == Qt::LeftButton)
            paintPixel(x, y, pen);
        else if (button == Qt::RightButton)
            paintPixel(x, y, State::PEN_BACKGROUND);

        _commandMergeable = true;
    }
}

void BigCharWidget::mouseReleaseEvent(QMouseEvent* event)
{
    event->accept();

    // don't merge "tilePaint" command if the mouse was
    // released
    _commandMergeable = false;
}

void BigCharWidget::keyPressEvent(QKeyEvent* event)
{
    event->accept();

    bool updated = false;
    bool ismc = _state->shouldBeDisplayedInMulticolor2(_tileIndex);
    auto oldCursorPos = _cursorPos;
    int increment_x = ismc ? 2 : 1;

    switch (event->key()) {
    case Qt::Key_Left:
        _cursorPos += { -increment_x, 0 };
        break;
    case Qt::Key_Right:
        _cursorPos += { +increment_x, 0 };
        break;
    case Qt::Key_Down:
        _cursorPos += { 0, +1 };
        break;
    case Qt::Key_Up:
        _cursorPos += { 0, -1 };
        break;
    case Qt::Key_1:
        paintPixel(_cursorPos.x(), _cursorPos.y(), State::PEN_BACKGROUND);
        updated = true;
        break;
    case Qt::Key_2:
        paintPixel(_cursorPos.x(), _cursorPos.y(), State::PEN_FOREGROUND);
        updated = true;
        break;
    case Qt::Key_3:
        if (ismc) {
            paintPixel(_cursorPos.x(), _cursorPos.y(), State::PEN_MULTICOLOR1);
            updated = true;
        }
        break;
    case Qt::Key_4:
        if (ismc) {
            paintPixel(_cursorPos.x(), _cursorPos.y(), State::PEN_MULTICOLOR2);
            updated = true;
        }
        break;
    case Qt::Key_Space:
        cyclePixel(_cursorPos.x(), _cursorPos.y());
        updated = true;
        break;
    default:
        QWidget::keyPressEvent(event);
    }

    _cursorPos = { qBound(0, _cursorPos.x(), 8 * _tileProperties.size.width() - increment_x),
        qBound(0, _cursorPos.y(), 8 * _tileProperties.size.height() - 1) };

    // redraw cursor
    if (oldCursorPos != _cursorPos)
        updated = true;

    if (updated) {
        update();

        MainWindow::getInstance()->showMessageOnStatusBar(tr("x: %1, y: %2")
                .arg(_cursorPos.x())
                .arg(_cursorPos.y()));
    }
}

void BigCharWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter;
    painter.begin(this);

    // paint with default background color
    painter.fillRect(event->rect(), QWidget::palette().color(QWidget::backgroundRole()));

    QPen pen;
    pen.setColor(QWidget::palette().color(QWidget::backgroundRole()));
    pen.setWidth(1);
    pen.setStyle(Qt::PenStyle::SolidLine);
    painter.setPen(pen);

    int charIndex = _charIndex;

    for (int y = 0; y < _tileProperties.size.height(); y++) {
        for (int x = 0; x < _tileProperties.size.width(); x++) {

            utilsDrawCharInPainter(_state, &painter, _pixelSize, QPoint(0, 0), QPoint(x, y), charIndex);
            charIndex += _tileProperties.interleaved;
        }
    }

    paintCursor(painter);
    paintSeparators(painter);
    paintFocus(painter);

    painter.end();
}

void BigCharWidget::paintCursor(QPainter& painter)
{
    painter.setBrush(QColor(0, 0, 255, 24));
    QPen pen;
    pen.setColor({ 149, 195, 244, 255 });
    pen.setStyle(Qt::PenStyle::SolidLine);

    if (hasFocus())
        pen.setWidth(3);
    else
        pen.setWidth(1);
    painter.setPen(pen);

    int bit_width = 1;
    int x = _cursorPos.x();
    if (_state->shouldBeDisplayedInMulticolor2(_tileIndex)) {
        bit_width = 2;

        // this is needed because cursorPos.x might be pointing
        // to the "half" bit when swtiching from HR to MC.
        x = (x / 2) * 2;
    }

    painter.drawRect(x * _pixelSize.width(),
        _cursorPos.y() * _pixelSize.height(),
        _pixelSize.width() * bit_width,
        _pixelSize.height());
}

void BigCharWidget::paintSeparators(QPainter& painter)
{
    QPen pen;
    pen.setColor({ 128, 128, 128, 196 });
    pen.setWidth(2);
    pen.setStyle(Qt::PenStyle::SolidLine);

    painter.setPen(pen);

    for (int x = 1; x <= _tileProperties.size.width() - 1; x++) {
        painter.drawLine(x * _pixelSize.width() * 8, 0,
            x * _pixelSize.width() * 8, _pixelSize.height() * _tileProperties.size.height() * 8);
    }
    for (int y = 1; y <= _tileProperties.size.height() - 1; y++) {
        painter.drawLine(0, y * _pixelSize.height() * 8,
            _pixelSize.width() * _tileProperties.size.width() * 8, y * _pixelSize.height() * 8);
    }
}

void BigCharWidget::paintFocus(QPainter& painter)
{
    if (hasFocus()) {
        QPen pen;
        pen.setColor({ 149, 195, 244, 255 });
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

bool BigCharWidget::maybeSave()
{
    if (_state->isModified()) {
        QMessageBox::StandardButton ret;
        ret = QMessageBox::warning(this, tr("Application"),
            tr("There are unsaved changes.\n"
               "Do you want to save your changes?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (ret == QMessageBox::Save)
            return MainWindow::getInstance()->on_actionSave_triggered();
        if (ret == QMessageBox::Cancel)
            return false;
    }
    return true;
}

//
// slots
//
void BigCharWidget::onMulticolorModeToggled(bool state)
{
    Q_UNUSED(state)
    update();
}

void BigCharWidget::onTileIndexUpdated(int tileIndex)
{
    if (_tileIndex != tileIndex) {
        _tileIndex = tileIndex;
        _charIndex = _state->getCharIndexFromTileIndex(_tileIndex);
        update();
    }
}

void BigCharWidget::onTilePropertiesUpdated()
{
    _tileProperties = _state->getTileProperties();

    // keep aspect ratio
    int maxTileSize = qMax(_tileProperties.size.width(), _tileProperties.size.height());
    int minWidgetSize = qMin(size().width(), size().height());
    _pixelSize.setWidth(minWidgetSize / (8 * maxTileSize));
    _pixelSize.setHeight(minWidgetSize / (8 * maxTileSize));

    //    _pixelSize.setWidth(size().width() / (8*_tileProperties.size.width()));
    //    _pixelSize.setHeight(size().height() / (8*_tileProperties.size.height()));

    update();
}

void BigCharWidget::onColorPropertiesUpdated(int pen)
{
    Q_UNUSED(pen)
    update();
}

void BigCharWidget::onTileUpdated(int tileIndex)
{
    Q_UNUSED(tileIndex)
    update();
}

void BigCharWidget::onCharsetUpdated()
{
    update();
}

void BigCharWidget::onFileLoaded()
{
    onTilePropertiesUpdated();
}

//
// overrides
//
void BigCharWidget::resizeEvent(QResizeEvent* event)
{
    Q_UNUSED(event)

    // keep aspect ratio
    int maxTileSize = qMax(_tileProperties.size.width(), _tileProperties.size.height());
    int minWidgetSize = qMin(size().width(), size().height());
    _pixelSize.setWidth(minWidgetSize / (8 * maxTileSize));
    _pixelSize.setHeight(minWidgetSize / (8 * maxTileSize));

    //    _pixelSize.setWidth(size().width() / (8*_tileProperties.size.width()));
    //    _pixelSize.setHeight(size().height() / (8*_tileProperties.size.height()));
}

void BigCharWidget::closeEvent(QCloseEvent* event)
{
    if (maybeSave()) {
        event->accept();
    } else {
        event->ignore();
    }
}
