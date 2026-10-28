/****************************************************************************
Copyright 2016-2026 Ricardo Quesada

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
 * @file vchar64application.cpp
 * @brief `QApplication` subclass intercepting macOS Finder `QEvent::FileOpen` events (`vchar64`).
 */

#include "vchar64application.h"

#include <QFileOpenEvent>

VChar64Application::VChar64Application(int& argc, char** argv)
    : QApplication(argc, argv)
{
}

bool VChar64Application::event(QEvent* event)
{
    // On macOS, double-clicking a `.vchar64proj` or `.ctm` file in Finder dispatches
    // an AppleEvent translated by Qt into `QEvent::FileOpen` rather than `argv[1]`.
    switch (event->type()) {
    case QEvent::FileOpen:
        emit fileOpenRequest(static_cast<QFileOpenEvent*>(event)->file());
        return true;
    default:
        return QApplication::event(event);
    }
}
