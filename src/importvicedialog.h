/****************************************************************************
Copyright 2015 Ricardo Quesada

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
class ImportVICEDialog;
}

class State;

class ImportVICEDialog : public QDialog
{
    friend class ImportVICECharsetWidget;
    friend class ImportVICEScreenRAMWidget;

    Q_OBJECT

public:
    explicit ImportVICEDialog(QWidget *parent = 0);
    ~ImportVICEDialog();

    const QString& getFilepath() const;

protected:
    bool validateVICEFile(const QString& filepath);
    void updateWidgets();
    void updateTileImages();

private slots:
    void on_pushButtonImport_clicked();

    void on_pushButtonCancel_clicked();

    void on_pushButtonBrowse_clicked();

    void on_lineEdit_editingFinished();

    void on_spinBoxCharset_editingFinished();

    void on_spinBoxScreenRAM_editingFinished();

    void on_spinBoxCharset_valueChanged(int address);

    void on_spinBoxScreenRAM_valueChanged(int address);

    void on_checkBoxMulticolor_clicked(bool checked);

    void on_checkBoxGuessColors_clicked(bool checked);

    void on_checkBoxDisplayGrid_clicked(bool checked);

    void on_checkBoxInvalidAddresses_clicked(bool checked);

private:
    Ui::ImportVICEDialog *ui;
    bool _validVICEFile;
    QString _filepath;

    quint8 _memoryRAM[64*1024];     // RAM: 64k
    quint8 _colorRAM[1024];         // Color RAM: 1k
    quint8 _VICColorsBackup[3];
    State* _tmpState;

    // To gain speed, each tile will be pre-renderer in a QImage
    // a QImages will be renderer
    QImage *_tileImages[256];

    bool _supportInvalidVICAddresses;

};
