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
 * @file preferences.cpp
 * @brief Implementation of the GUI-layer `QSettings` persistence facade.
 *
 * Clamps persisted palette indices to `[0, Palette::kPaletteCount - 1]` and
 * update-check day deltas to `[0, 36500]` to guard against corrupted or out-of-range
 * user configuration entries.
 */

#include "preferences.h"

#include <algorithm>

#include <QColor>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QTime>

#include "palette.h"

Preferences& Preferences::getInstance()
{
    static Preferences prefs;
    return prefs;
}

Preferences::Preferences()
    : _settings(QStringLiteral("RetroMoe"), QStringLiteral("VChar64"))
{
    readSettings();
    qDebug() << _settings.fileName();
}

Preferences::~Preferences() = default;

void Preferences::readSettings()
{
    const int rawPalette = _settings.value(QLatin1String("palette"), 0).toInt();
    _palette = std::clamp(rawPalette, 0, Palette::kPaletteCount - 1);
}

void Preferences::setGridColor(const QColor& color)
{
    const QVariant variantColor = color;
    _settings.setValue(QLatin1String("UI/gridColor"), variantColor);
}

QColor Preferences::getGridColor() const
{
    return _settings.value(QLatin1String("UI/gridColor"), QColor(0, 128, 0)).value<QColor>();
}

void Preferences::setOpenLastFiles(bool enableIt)
{
    _settings.setValue(QLatin1String("Startup/OpenLastFiles"), enableIt);
}

bool Preferences::getOpenLastFiles() const
{
    return _settings.value(QLatin1String("Startup/OpenLastFiles"), true).toBool();
}

void Preferences::setCheckUpdates(bool enableIt)
{
    _settings.setValue(QLatin1String("Install/CheckForUpdates"), enableIt);
}

bool Preferences::getCheckUpdates() const
{
    return _settings.value(QLatin1String("Install/CheckForUpdates"), true).toBool();
}

void Preferences::setLastUpdateCheckDate(const QDateTime& date)
{
    _settings.setValue(QLatin1String("Install/LastUpdateCheckDate"), date);
}

QDateTime Preferences::getLastUpdateCheckDate() const
{
    return _settings.value(
                        QLatin1String("Install/LastUpdateCheckDate"),
                        QDateTime(QDate(2015, 1, 1), QTime(0, 0)))
        .toDateTime();
}

int Preferences::getLastTimeUpdateCheck() const
{
    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime lastTime = getLastUpdateCheckDate();
    const qint64 days = lastTime.daysTo(now);
    return static_cast<int>(std::clamp<qint64>(days, 0, 36500));
}

// default geometry / state
void Preferences::setMainWindowDefaultGeometry(const QByteArray& geometry)
{
    _settings.setValue(QLatin1String("MainWindow/defaultGeometry"), geometry);
}

QByteArray Preferences::getMainWindowDefaultGeometry() const
{
    return _settings.value(QLatin1String("MainWindow/defaultGeometry")).toByteArray();
}

void Preferences::setMainWindowDefaultState(const QByteArray& state)
{
    _settings.setValue(QLatin1String("MainWindow/defaultWindowState"), state);
}

QByteArray Preferences::getMainWindowDefaultState() const
{
    return _settings.value(QLatin1String("MainWindow/defaultWindowState")).toByteArray();
}

// user geometry / state
void Preferences::setMainWindowGeometry(const QByteArray& geometry)
{
    _settings.setValue(QLatin1String("MainWindow/geometry"), geometry);
}

QByteArray Preferences::getMainWindowGeometry() const
{
    return _settings.value(QLatin1String("MainWindow/geometry")).toByteArray();
}

void Preferences::setMainWindowState(const QByteArray& state)
{
    _settings.setValue(QLatin1String("MainWindow/windowState"), state);
}

QByteArray Preferences::getMainWindowState() const
{
    return _settings.value(QLatin1String("MainWindow/windowState")).toByteArray();
}

// palette index
void Preferences::setPalette(int palette)
{
    _palette = std::clamp(palette, 0, Palette::kPaletteCount - 1);
    _settings.setValue(QLatin1String("palette"), _palette);
}

int Preferences::getPalette() const
{
    const int rawPalette = _settings.value(QLatin1String("palette"), _palette).toInt();
    return std::clamp(rawPalette, 0, Palette::kPaletteCount - 1);
}

// recent files
void Preferences::setRecentFiles(const QStringList& filenames)
{
    _settings.setValue(QLatin1String("recentFiles/fileNames"), filenames);
}

QStringList Preferences::getRecentFiles() const
{
    return _settings.value(QLatin1String("recentFiles/fileNames")).toStringList();
}

// session files
void Preferences::setSessionFiles(const QStringList& filenames)
{
    _settings.setValue(QLatin1String("sessionFiles/fileNames"), filenames);
}

QStringList Preferences::getSessionFiles() const
{
    return _settings.value(QLatin1String("sessionFiles/fileNames")).toStringList();
}

// last dir
void Preferences::setLastUsedDirectory(const QString& path)
{
    _settings.setValue(QLatin1String("dir/lastdir"), path);
}

QString Preferences::getLastUsedDirectory() const
{
    return _settings.value(QLatin1String("dir/lastdir"), QDir::homePath()).toString();
}

// open filter
void Preferences::setLastUsedOpenFilter(const QString& filter)
{
    _settings.setValue(QLatin1String("dir/lastUsedOpenFilter"), filter);
}

QString Preferences::getLastUsedOpenFilter() const
{
    return _settings.value(QLatin1String("dir/lastUsedOpenFilter"), tr("All supported files")).toString();
}

// server IP Address
void Preferences::setServerIPAddress(const QString& ipaddress)
{
    _settings.setValue(QLatin1String("server/ipaddress"), ipaddress);
}

QString Preferences::getServerIPAddress() const
{
    return _settings.value(QLatin1String("server/ipaddress"), QStringLiteral("10.0.1.64")).toString();
}
