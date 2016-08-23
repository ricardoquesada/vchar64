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

#include "preferences.h"

#include <QSettings>
#include <QColor>

Preferences& Preferences::getInstance()
{
    static Preferences prefs;
    return prefs;
}

Preferences::Preferences()
{
}

Preferences::~Preferences()
{
}

void Preferences::setGridColor(const QColor& color)
{
    QVariant variantColor = color;
    QSettings().setValue(QLatin1String("UI/gridColor"), variantColor);
}

QColor Preferences::getGridColor() const
{
    return QSettings().value(QLatin1String("UI/gridColor"), QColor(0,128,0)).value<QColor>();
}

void Preferences::setOpenLastFiles(bool enableIt)
{
    QSettings().setValue(QLatin1String("Startup/OpenLastFiles"), enableIt);
}

bool Preferences::getOpenLastFiles() const
{
    return QSettings().value(QLatin1String("Startup/OpenLastFiles"), true).toBool();
}

void Preferences::setCheckUpdates(bool enableIt)
{
    QSettings().setValue(QLatin1String("Install/CheckForUpdates"), enableIt);
}

bool Preferences::getCheckUpdates() const
{
    return QSettings().value(QLatin1String("Install/CheckForUpdates"), true).toBool();
}

void Preferences::setLastUpdateCheckDate(const QDateTime& date)
{
    QSettings().setValue(QLatin1String("Install/LastUpdateCheckDate"), date);
}

QDateTime Preferences::getLastUpdateCheckDate() const
{
    return QSettings().value(QLatin1String("Install/LastUpdateCheckDate"), QDateTime()).toDateTime();
}

int Preferences::getLastTimeUpdateCheck() const
{
    QDateTime now = QDateTime::currentDateTime();
    QDateTime lastTime = getLastUpdateCheckDate();

    return lastTime.daysTo(now);
}
