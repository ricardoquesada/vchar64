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

void Preferences::setColorGrid(const QColor& color)
{
    QVariant variantColor = color;
    QSettings().setValue(QLatin1String("colorGrid"), variantColor);
}

QColor Preferences::getColorGrid() const
{
    return QSettings().value(QLatin1String("colorGrid"), QColor(0,128,0)).value<QColor>();
}

void Preferences::setSaveSession(bool enableIt)
{
    QSettings().setValue(QLatin1String("saveSession"), enableIt);
}

bool Preferences::getSaveSession() const
{
    return QSettings().value(QLatin1String("saveSession"), true).toBool();
}

void Preferences::setCheckUpdates(bool enableIt)
{
    QSettings().setValue(QLatin1String("checkUpdates"), enableIt);
}

bool Preferences::getCheckUpdates() const
{
    return QSettings().value(QLatin1String("checkUpdates"), true).toBool();
}
