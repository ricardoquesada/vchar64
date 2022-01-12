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

#include "state.h"
#include <QWidget>

class ImportVICEDialog;

class ImportVICECharsetWidget : public QWidget {
    Q_OBJECT

public:
    ImportVICECharsetWidget(QWidget* parent = nullptr);
    void setParentDialog(ImportVICEDialog* parentDialog) { _parentDialog = parentDialog; }
    void setDisplayGrid(bool enabled);

protected:
    void paintEvent(QPaintEvent* event) Q_DECL_OVERRIDE;
    ImportVICEDialog* _parentDialog;
    bool _displayGrid;
};
