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
    Q_OBJECT

public:
    explicit ImportVICEDialog(QWidget *parent = 0);
    ~ImportVICEDialog();

    const QString& getFilepath() const;

protected:
    bool validateVICEFile(const QString& filepath);
    void updateWidgets();

private slots:
    void on_pushButton_import_clicked();

    void on_pushButton_cancel_clicked();

    void on_spinBox_editingFinished();

    void on_spinBox_valueChanged(int value);

    void on_pushButton_clicked();

    void on_lineEdit_editingFinished();

private:
    Ui::ImportVICEDialog *ui;
    bool _validVICEFile;
    QString _filepath;
};
