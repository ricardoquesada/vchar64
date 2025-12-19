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

#include <QDialog>
#include <array>
#include <memory>
#include <unordered_map>

namespace Ui {
class ImportKoalaDialog;
}

class ImportKoalaDialog : public QDialog {
    Q_OBJECT

public:
    explicit ImportKoalaDialog(QWidget* parent = nullptr);
    virtual ~ImportKoalaDialog() override;

    const QString& getFilepath() const;

protected:
    void mousePressEvent(QMouseEvent* event) override;

    void validateKoalaFile(const QString& filepath);
    bool processChardef(const std::string& key, std::array<quint8, 8>& outKey, quint8& outColorRAM);

    int findColorRAM(const std::vector<std::pair<int, int>>& usedColors, int& outHiColor);

    void normalizeKey(std::array<char, 32>& key, int hiColorRAM);
    void normalizeWithColorStrategy(std::array<char, 32>& key, int hiColorRAM);
    void normalizeWithNeighborStrategy(std::array<char, 32>& key, int hiColorRAM);
    bool tryChangeKey(int x, int y, std::array<char, 32>& key, quint8 mask, int hiColorRAM);
    int getColorByLuminanceProximity(int colorIndex, const std::vector<int>& colorsToFind);
    int getColorByPaletteProximity(int colorIndex, const std::vector<int>& colorsToFind);

    bool convert();
    void updateWidgets();

private slots:
    void on_radioD02xManual_toggled(bool checked);
    void on_radioD02xMostUsed_toggled(bool checked);
    void on_radioD02xMostUsedHi_toggled(bool checked);
    void on_radioForegroundMostUsed_toggled(bool checked);
    void on_radioForegroundMostUsedLow_toggled(bool checked);
    void on_radioButtonLuminance_toggled(bool checked);
    void on_radioButtonPalette_toggled(bool checked);
    void on_radioButtonNeighbor_toggled(bool checked);

    void on_checkBoxGrid_toggled(bool checked);

    // browse directories
    void on_pushButtonBrowse_clicked();
    void on_lineEdit_editingFinished();

    void on_pushButtonImport_clicked();
    void on_pushButtonCancel_clicked();

    void onSelectedRegionUpdated(const QRect& region);

private:
    int _colorRAM = -1;
    std::unique_ptr<Ui::ImportKoalaDialog> ui;
    bool _validKoalaFile = false;
    bool _koaLoaded = false;
    QString _filepath;

    // the difference between bitmap->uniqueCells and this one
    // is that _uniqueCells is about "bitmaps" unique cells.
    // and where is about the converted chars, which is usually less
    // since it could have more duplicates
    std::unordered_map<std::string, std::vector<std::pair<int, int>>> _uniqueChars;
};
