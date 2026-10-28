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
 * @file preferences.h
 * @brief GUI-layer `QSettings` persistence singleton for user preferences and window state.
 *
 * Isolates all platform `QSettings` (`RetroMoe` / `VChar64`) I/O inside the top-level
 * `vchar64` GUI target so headless `vchar64_core` (`Palette`, `State`, importers,
 * exporters) and `vchar64_net` (`ServerPreview`) never touch global disk settings
 * during unit tests or CLI operations.
 */

#pragma once

#include <QDateTime>
#include <QObject>
#include <QSettings>

/**
 * @brief Process-wide `QSettings` facade storing UI layout, recent/session files,
 *        active C64 palette selection, update-check timestamps, and live-preview host IP.
 */
class Preferences : public QObject {
    Q_OBJECT

public:
    static Preferences& getInstance();

    Preferences(Preferences const&) = delete;
    void operator=(Preferences const&) = delete;

    /** @brief Reloads persisted palette selection clamped to `[0, Palette::kPaletteCount - 1]`. */
    void readSettings();

    void setGridColor(const QColor& color);
    [[nodiscard]] QColor getGridColor() const;

    void setOpenLastFiles(bool enableIt);
    [[nodiscard]] bool getOpenLastFiles() const;

    void setCheckUpdates(bool enableIt);
    [[nodiscard]] bool getCheckUpdates() const;

    void setLastUpdateCheckDate(const QDateTime& date);
    [[nodiscard]] QDateTime getLastUpdateCheckDate() const;
    [[nodiscard]] int getLastTimeUpdateCheck() const;

    void setMainWindowDefaultGeometry(const QByteArray& geometry);
    [[nodiscard]] QByteArray getMainWindowDefaultGeometry() const;
    void setMainWindowDefaultState(const QByteArray& state);
    [[nodiscard]] QByteArray getMainWindowDefaultState() const;

    void setMainWindowGeometry(const QByteArray& geometry);
    [[nodiscard]] QByteArray getMainWindowGeometry() const;
    void setMainWindowState(const QByteArray& state);
    [[nodiscard]] QByteArray getMainWindowState() const;

    void setPalette(int palette);
    [[nodiscard]] int getPalette() const;

    void setRecentFiles(const QStringList& filenames);
    [[nodiscard]] QStringList getRecentFiles() const;

    void setSessionFiles(const QStringList& filenames);
    [[nodiscard]] QStringList getSessionFiles() const;

    void setLastUsedDirectory(const QString& path);
    [[nodiscard]] QString getLastUsedDirectory() const;

    void setLastUsedOpenFilter(const QString& filter);
    [[nodiscard]] QString getLastUsedOpenFilter() const;

    void setServerIPAddress(const QString& ipaddress);
    [[nodiscard]] QString getServerIPAddress() const;

private:
    Preferences();
    ~Preferences() override;

    QSettings _settings;
    int _palette = 0;
};
