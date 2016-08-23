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

#include <QObject>
#include <QDateTime>

class Preferences: public QObject
{
    Q_OBJECT

public:
    static Preferences& getInstance();

    Preferences(Preferences const&)     = delete;
    void operator=(Preferences const&)  = delete;

    void setGridColor(const QColor& color);
    QColor getGridColor() const;

    void setOpenLastFiles(bool enableIt);
    bool getOpenLastFiles() const;

    void setCheckUpdates(bool enableIt);
    bool getCheckUpdates() const;

    void setLastUpdateCheckDate(const QDateTime& date);
    QDateTime getLastUpdateCheckDate() const;

private:
    Preferences();
    ~Preferences();
};
