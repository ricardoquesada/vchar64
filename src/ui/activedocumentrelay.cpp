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

// Implements ActiveDocumentRelay, maintaining O(1) signal connections exclusively
// to the single active MDI State document and its QUndoStack.

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

void ActiveDocumentRelay::setActiveState(State* next_state)
{
    if (_boundRawState == next_state) {
        return;
    }

    detachConnections();
    _activeState = next_state;
    _boundRawState = next_state;

    if (next_state == nullptr) {
        emit activeStateChanged(nullptr);
        return;
    }

    _activeConnections.reserve(18);

    _activeConnections.push_back(
        connect(next_state, &State::fileLoaded, this, &ActiveDocumentRelay::fileLoaded));
    _activeConnections.push_back(
        connect(next_state, &State::tilePropertiesUpdated, this, &ActiveDocumentRelay::tilePropertiesUpdated));
    _activeConnections.push_back(
        connect(next_state, &State::mapSizeUpdated, this, &ActiveDocumentRelay::mapSizeUpdated));
    _activeConnections.push_back(
        connect(next_state, &State::mapContentUpdated, this, &ActiveDocumentRelay::mapContentUpdated));
    _activeConnections.push_back(
        connect(next_state, &State::bytesUpdated, this, &ActiveDocumentRelay::bytesUpdated));
    _activeConnections.push_back(
        connect(next_state, &State::tileUpdated, this, &ActiveDocumentRelay::tileUpdated));
    _activeConnections.push_back(
        connect(next_state, &State::charsetUpdated, this, &ActiveDocumentRelay::charsetUpdated));
    _activeConnections.push_back(
        connect(next_state, &State::colorPropertiesUpdated, this, &ActiveDocumentRelay::colorPropertiesUpdated));
    _activeConnections.push_back(
        connect(next_state, &State::selectedPenChanged, this, &ActiveDocumentRelay::selectedPenChanged));
    _activeConnections.push_back(
        connect(next_state, &State::multicolorModeToggled, this, &ActiveDocumentRelay::multicolorModeToggled));
    _activeConnections.push_back(
        connect(next_state, &State::contentsChanged, this, &ActiveDocumentRelay::contentsChanged));
    _activeConnections.push_back(
        connect(next_state, &State::charIndexUpdated, this, &ActiveDocumentRelay::charIndexUpdated));
    _activeConnections.push_back(
        connect(next_state, &State::tileIndexUpdated, this, &ActiveDocumentRelay::tileIndexUpdated));

    if (QUndoStack* undo_stack = next_state->getUndoStack()) {
        _activeConnections.push_back(
            connect(undo_stack, &QUndoStack::cleanChanged, this, &ActiveDocumentRelay::undoCleanChanged));
        _activeConnections.push_back(
            connect(undo_stack, &QUndoStack::canUndoChanged, this, &ActiveDocumentRelay::canUndoChanged));
        _activeConnections.push_back(
            connect(undo_stack, &QUndoStack::canRedoChanged, this, &ActiveDocumentRelay::canRedoChanged));
        _activeConnections.push_back(
            connect(undo_stack, &QUndoStack::indexChanged, this, &ActiveDocumentRelay::undoIndexChanged));
    }

    // Compare against _boundRawState because QPointer clears its internal weak reference
    // prior to QObject::destroyed slot invocation during ~QObject().
    _activeConnections.push_back(
        connect(next_state, &QObject::destroyed, this, [this, next_state]() {
            if (_boundRawState == next_state) {
                detachConnections();
                _activeState = nullptr;
                _boundRawState = nullptr;
                emit activeStateChanged(nullptr);
            }
        }));

    emit activeStateChanged(next_state);
}

void ActiveDocumentRelay::detachConnections() noexcept
{
    for (const QMetaObject::Connection& connection : _activeConnections) {
        QObject::disconnect(connection);
    }
    _activeConnections.clear();
}
