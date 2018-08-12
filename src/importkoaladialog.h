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

#include <unordered_map>
#include <QDialog>

namespace Ui {
class ImportKoalaDialog;
}

class ImportKoalaDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ImportKoalaDialog(QWidget *parent = 0);
    ~ImportKoalaDialog();

    const QString& getFilepath() const;

protected:
    void mousePressEvent(QMouseEvent* event) Q_DECL_OVERRIDE;

    void validateKoalaFile(const QString& filepath);
    bool processChardef(const std::string& key, quint8 *outKey, quint8* outColorRAM);

    int findColorRAM(const std::vector<std::pair<int,int>>& usedColors, int *outHiColor);

    void normalizeKey(char* key, int hiColorRAM);
    void normalizeWithColorStrategy(char* key, int hiColorRAM);
    void normalizeWithNeighborStrategy(char* key, int hiColorRAM);
    bool tryChangeKey(int x, int y, char* key, quint8 mask, int hiColorRAM);
    int getColorByLuminanceProximity(int colorIndex, const std::vector<int> &colorsToFind);
    int getColorByPaletteProximity(int colorIndex, const std::vector<int> &colorsToFind);

    bool convert();
    void updateWidgets();

private slots:
    void on_radioD02xManual_clicked();
    void on_radioD02xMostUsed_clicked();
    void on_radioD02xMostUsedHi_clicked();
    void on_radioForegroundMostUsed_clicked();
    void on_radioForegroundMostUsedLow_clicked();
    void on_radioButtonLuminance_clicked();
    void on_radioButtonPalette_clicked();
    void on_radioButtonNeighbor_clicked();

    void on_checkBoxGrid_clicked(bool checked);

    // browse directories
    void on_pushButtonBrowse_clicked();
    void on_lineEdit_editingFinished();

    void on_pushButtonImport_clicked();
    void on_pushButtonCancel_clicked();

    void onSelectedRegionUpdated(const QRect& region);

private:

    int _colorRAM;
    Ui::ImportKoalaDialog *ui;
    bool _validKoalaFile;
    bool _koaLoaded;
    QString _filepath;

    // the difference between bitmap->uniqueCells and this one
    // is that _uniqueCells is about "bitmaps" unique cells.
    // and where is about the converted chars, which is usually less
    // since it could have more duplicates
    std::unordered_map<std::string, std::vector<std::pair<int,int>>> _uniqueChars;
};
