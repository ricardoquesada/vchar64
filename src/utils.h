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

#pragma once

#include <QtGlobal>

QT_BEGIN_NAMESPACE
class QPainter;
class QImage;
class QPoint;
class QSizeF;
QT_END_NAMESPACE

class State;

void utilsDrawCharInPainter(State* state, QPainter* painter, const QSizeF& pixelSize, const QPoint& offset, const QPoint& orig, int charIdx);
void utilsDrawCharInImage(State* state, QImage* image, const QPoint& offset, int charIdx);
quint8 utilsAsciiToScreenCode(quint8 ascii);
quint8 utilsAsciiToAtari8Bit(quint8 ascii);
