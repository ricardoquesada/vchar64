#ifndef IMPORTVICESCREENRAMWIDGET_H
#define IMPORTVICESCREENRAMWIDGET_H

#include <QWidget>

class ImportVICEDialog;

class ImportVICEScreenRAMWidget : public QWidget {
    Q_OBJECT
public:
    explicit ImportVICEScreenRAMWidget(QWidget* parent = nullptr);
    void setParentDialog(ImportVICEDialog* parentDialog) { _parentDialog = parentDialog; }
    void setDisplayGrid(bool enabled);

protected:
    void paintEvent(QPaintEvent* event) Q_DECL_OVERRIDE;

    void updateTileImages();
    ImportVICEDialog* _parentDialog; // weak ref
    bool _displayGrid;
};

#endif // IMPORTVICESCREENRAMWIDGET_H
