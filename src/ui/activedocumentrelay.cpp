/****************************************************************************
Copyright 2026 Ricardo Quesada

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

/**
 * @file activedocumentrelay.cpp
 * @brief Implementation of `ActiveDocumentRelay` (`vchar64` GUI target).
 *
 * Replaces O(N * M) cross-document signal fan-out across MDI tabs with an O(1)
 * active connection set stored in `_activeConnections`.
 */

#include "ui/activedocumentrelay.h"

#include <QUndoStack>

ActiveDocumentRelay::ActiveDocumentRelay(QObject* parent)
    : QObject(parent)
{
}

ActiveDocumentRelay::~ActiveDocumentRelay()
{
    detachConnections();
}

void ActiveDocumentRelay::detachConnections() noexcept
{
    for (const QMetaObject::Connection& conn : _activeConnections) {
        QObject::disconnect(conn);
    }
    _activeConnections.clear();
}

void ActiveDocumentRelay::setActiveState(State* nextState)
{
    if (_boundRawState == nextState) {
        return;
    }

    detachConnections();
    _activeState = nextState;
    _boundRawState = nextState;

    if (nextState != nullptr) {
        _activeConnections.reserve(18);

        _activeConnections.push_back(
            connect(nextState, &State::fileLoaded, this, &ActiveDocumentRelay::fileLoaded));
        _activeConnections.push_back(
            connect(nextState, &State::tilePropertiesUpdated, this, &ActiveDocumentRelay::tilePropertiesUpdated));
        _activeConnections.push_back(
            connect(nextState, &State::mapSizeUpdated, this, &ActiveDocumentRelay::mapSizeUpdated));
        _activeConnections.push_back(
            connect(nextState, &State::mapContentUpdated, this, &ActiveDocumentRelay::mapContentUpdated));
        _activeConnections.push_back(
            connect(nextState, &State::bytesUpdated, this, &ActiveDocumentRelay::bytesUpdated));
        _activeConnections.push_back(
            connect(nextState, &State::tileUpdated, this, &ActiveDocumentRelay::tileUpdated));
        _activeConnections.push_back(
            connect(nextState, &State::charsetUpdated, this, &ActiveDocumentRelay::charsetUpdated));
        _activeConnections.push_back(
            connect(nextState, &State::colorPropertiesUpdated, this, &ActiveDocumentRelay::colorPropertiesUpdated));
        _activeConnections.push_back(
            connect(nextState, &State::selectedPenChanged, this, &ActiveDocumentRelay::selectedPenChanged));
        _activeConnections.push_back(
            connect(nextState, &State::multicolorModeToggled, this, &ActiveDocumentRelay::multicolorModeToggled));
        _activeConnections.push_back(
            connect(nextState, &State::contentsChanged, this, &ActiveDocumentRelay::contentsChanged));
        _activeConnections.push_back(
            connect(nextState, &State::charIndexUpdated, this, &ActiveDocumentRelay::charIndexUpdated));
        _activeConnections.push_back(
            connect(nextState, &State::tileIndexUpdated, this, &ActiveDocumentRelay::tileIndexUpdated));

        if (QUndoStack* stack = nextState->getUndoStack()) {
            _activeConnections.push_back(
                connect(stack, &QUndoStack::cleanChanged, this, &ActiveDocumentRelay::undoCleanChanged));
            _activeConnections.push_back(
                connect(stack, &QUndoStack::canUndoChanged, this, &ActiveDocumentRelay::canUndoChanged));
            _activeConnections.push_back(
                connect(stack, &QUndoStack::canRedoChanged, this, &ActiveDocumentRelay::canRedoChanged));
            _activeConnections.push_back(
                connect(stack, &QUndoStack::indexChanged, this, &ActiveDocumentRelay::undoIndexChanged));
        }

        // Compare against _boundRawState because QPointer clears its internal weak reference
        // prior to QObject::destroyed slot invocation during ~QObject().
        _activeConnections.push_back(
            connect(nextState, &QObject::destroyed, this, [this, nextState]() {
                if (_boundRawState == nextState) {
                    detachConnections();
                    _activeState = nullptr;
                    _boundRawState = nullptr;
                    emit activeStateChanged(nullptr);
                }
            }));
    }

    emit activeStateChanged(nextState);
}
