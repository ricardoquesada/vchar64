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

// GUI-layer QSettings persistence facade.
//
// Clamps persisted palette indices to [0, Palette::kPaletteCount - 1] and update-check
// day deltas to [0, 36500] to guard against corrupted or out-of-range configuration entries.

#include "preferences.h"

#include <algorithm>

#include <QColor>
#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QLatin1String>
#include <QTime>
#include <QVariant>

#include "palette.h"

Preferences& Preferences::getInstance()
{
    static Preferences instance;
    return instance;
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
    const int raw_palette = _settings.value(QLatin1String("palette"), 0).toInt();
    _palette = std::clamp(raw_palette, 0, Palette::kPaletteCount - 1);
}

void Preferences::setGridColor(const QColor& color)
{
    const QVariant variant_color = color;
    _settings.setValue(QLatin1String("UI/gridColor"), variant_color);
}

QColor Preferences::getGridColor() const
{
    return _settings.value(QLatin1String("UI/gridColor"), QColor(0, 128, 0)).value<QColor>();
}

void Preferences::setOpenLastFiles(bool enable_it)
{
    _settings.setValue(QLatin1String("Startup/OpenLastFiles"), enable_it);
}

bool Preferences::getOpenLastFiles() const
{
    return _settings.value(QLatin1String("Startup/OpenLastFiles"), true).toBool();
}

void Preferences::setCheckUpdates(bool enable_it)
{
    _settings.setValue(QLatin1String("Install/CheckForUpdates"), enable_it);
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
    const QDateTime last_check_time = getLastUpdateCheckDate();
    const qint64 days_elapsed = last_check_time.daysTo(now);
    return static_cast<int>(std::clamp<qint64>(days_elapsed, 0, 36500));
}

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

void Preferences::setPalette(int palette)
{
    _palette = std::clamp(palette, 0, Palette::kPaletteCount - 1);
    _settings.setValue(QLatin1String("palette"), _palette);
}

int Preferences::getPalette() const
{
    const int raw_palette = _settings.value(QLatin1String("palette"), _palette).toInt();
    return std::clamp(raw_palette, 0, Palette::kPaletteCount - 1);
}

void Preferences::setRecentFiles(const QStringList& filenames)
{
    _settings.setValue(QLatin1String("recentFiles/fileNames"), filenames);
}

QStringList Preferences::getRecentFiles() const
{
    return _settings.value(QLatin1String("recentFiles/fileNames")).toStringList();
}

void Preferences::setSessionFiles(const QStringList& filenames)
{
    _settings.setValue(QLatin1String("sessionFiles/fileNames"), filenames);
}

QStringList Preferences::getSessionFiles() const
{
    return _settings.value(QLatin1String("sessionFiles/fileNames")).toStringList();
}

void Preferences::setLastUsedDirectory(const QString& path)
{
    _settings.setValue(QLatin1String("dir/lastdir"), path);
}

QString Preferences::getLastUsedDirectory() const
{
    return _settings.value(QLatin1String("dir/lastdir"), QDir::homePath()).toString();
}

void Preferences::setLastUsedOpenFilter(const QString& filter)
{
    _settings.setValue(QLatin1String("dir/lastUsedOpenFilter"), filter);
}

QString Preferences::getLastUsedOpenFilter() const
{
    return _settings.value(QLatin1String("dir/lastUsedOpenFilter"), tr("All supported files")).toString();
}

void Preferences::setServerIPAddress(const QString& ip_address)
{
    _settings.setValue(QLatin1String("server/ipaddress"), ip_address);
}

QString Preferences::getServerIPAddress() const
{
    return _settings.value(QLatin1String("server/ipaddress"), QStringLiteral("10.0.1.64")).toString();
}
