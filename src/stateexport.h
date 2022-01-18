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

#pragma once

#include <QFile>

class State;

class StateExport {
public:
    static qint64 saveVChar64(State* state, QFile& file);

    static qint64 saveRaw(const QString& filename, const void* buffer, qsizetype bufferSize);
    static qint64 savePRG(const QString& filename, const void* buffer, qsizetype bufferSize, quint16 address);
    static qint64 saveAsm(const QString& filename, const void* buffer, qsizetype bufferSize, const QString& label);
    static qint64 saveC(const QString& filename, const void* buffer, qsizetype bufferSize, const QString& label);
};
