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
 * @file activedocumentrelay.h
 * @brief Single-connection MDI active-document signal multiplexer (`vchar64` GUI target).
 */

#pragma once

#include <vector>

#include <QMetaObject>
#include <QObject>
#include <QPointer>

#include "state.h"

/**
 * @brief Centralizes MDI active-document signal multiplexing.
 *
 * Maintains live Qt signal connections exclusively to the single currently
 * active `State` (and its `QUndoStack`). Inactive MDI documents have zero
 * live connections to global docks, toolbars, status bar labels,
 * `TileAtlasCache` instances, or `ILivePreviewSink` transports.
 */
class ActiveDocumentRelay : public QObject {
    Q_OBJECT

public:
    explicit ActiveDocumentRelay(QObject* parent = nullptr);
    ~ActiveDocumentRelay() override;

    /** @brief Returns the currently bound MDI `State` document, or `nullptr` if no subwindow is active. */
    [[nodiscard]] State* activeState() const noexcept
    {
        return _activeState.data();
    }

public slots:
    /**
     * @brief Detaches all 18 signal forwarders from the previous document and attaches to `nextState`.
     *
     * Passing `nullptr` detaches all connections cleanly and emits `activeStateChanged(nullptr)`
     * so docks, actions, and `ILivePreviewSink` enter their disabled empty-workspace state.
     */
    void setActiveState(State* nextState);

signals:
    void activeStateChanged(State* activeState);

    void fileLoaded();
    void tilePropertiesUpdated();
    void mapSizeUpdated();
    void mapContentUpdated();
    void bytesUpdated(int pos, int count);
    void tileUpdated(int tileIndex);
    void charsetUpdated();
    void colorPropertiesUpdated(int pen);
    void selectedPenChanged(int pen);
    void multicolorModeToggled(bool enabled);
    void contentsChanged();
    void charIndexUpdated(int charIndex);
    void tileIndexUpdated(int tileIndex);

    void undoCleanChanged(bool clean);
    void canUndoChanged(bool canUndo);
    void canRedoChanged(bool canRedo);
    void undoIndexChanged(int idx);

private:
    void detachConnections() noexcept;

    QPointer<State> _activeState;
    State* _boundRawState = nullptr;
    std::vector<QMetaObject::Connection> _activeConnections;
};
