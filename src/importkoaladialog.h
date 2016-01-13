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
    bool validateKoalaFile(const QString& filepath);
    bool processChardef(const std::string& key, quint8 *outKey, quint8* outColorRAM);

    int findColorRAM(const std::vector<std::pair<int,int>>& usedColors, int *outHiColor);

    void simplifyKey(char* key, int hiColorRAM);
    bool tryChangeKey(int x, int y, char* key, quint8 mask, int hiColorRAM);
    void convert();

private slots:
    void on_pushButton_clicked();

    void on_radioForegroundMostUsed_clicked();

    void on_radioForegroundMostUsedLow_clicked();

    void on_radioMostUsedColors_clicked();

    void on_radioMostUsedHiColors_clicked();

    void on_radioManual_clicked();

    void on_checkBoxGrid_clicked();

    void on_pushButton_6_clicked();

    void on_pushButton_5_clicked();

private:

    int _colorRAM;
    Ui::ImportKoalaDialog *ui;
    bool _validKoalaFile;
    QString _filepath;
};
