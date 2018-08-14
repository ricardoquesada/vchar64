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

#include <QColor>
#include <QDir>
#include <QDebug>

Preferences& Preferences::getInstance()
{
    static Preferences prefs;
    return prefs;
}

Preferences::Preferences()
    : _settings("RetroMoe","VChar64")
{
    qDebug() << _settings.fileName();
}

Preferences::~Preferences()
= default;

void Preferences::setGridColor(const QColor& color)
{
    QVariant variantColor = color;
    _settings.setValue(QLatin1String("UI/gridColor"), variantColor);
}

QColor Preferences::getGridColor() const
{
    return _settings.value(QLatin1String("UI/gridColor"), QColor(0,128,0)).value<QColor>();
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
    return _settings.value(QLatin1String("Install/LastUpdateCheckDate"), QDateTime(QDate(2015,1,1))).toDateTime();
}

int Preferences::getLastTimeUpdateCheck() const
{
    QDateTime now = QDateTime::currentDateTime();
    QDateTime lastTime = getLastUpdateCheckDate();

    return lastTime.daysTo(now);
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
    _settings.setValue(QLatin1String("palette"), palette);
}

int Preferences::getPalette() const
{
    return _settings.value(QLatin1String("palette"), 0).toInt();
}

// recent files

void Preferences::setRecentFiles(const QStringList& filenames)
{
    _settings.setValue(QLatin1String("recentFiles/fileNames"), filenames);
}

QStringList Preferences::getRecentFiles() const
{
    QVariant v = _settings.value(QLatin1String("recentFiles/fileNames"));
    return v.toStringList();
}

// session files

void Preferences::setSessionFiles(const QStringList& filenames)
{
    _settings.setValue(QLatin1String("sessionFiles/fileNames"), filenames);
}

QStringList Preferences::getSessionFiles() const
{
    QVariant v = _settings.value(QLatin1String("sessionFiles/fileNames"));
    return v.toStringList();
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
    return _settings.value(QLatin1String("server/ipaddress"), "10.0.1.64").toString();
}
