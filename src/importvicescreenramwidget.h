#ifndef IMPORTVICESCREENRAMWIDGET_H
#define IMPORTVICESCREENRAMWIDGET_H

#include <QWidget>

class ImportVICEDialog;

class ImportVICEScreenRAMWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ImportVICEScreenRAMWidget(QWidget *parent = 0);
    void setParentDialog(ImportVICEDialog* parentDialog) { _parentDialog = parentDialog; }
    void addressChanged(int address);
    void multicolorToggled(bool checked);

protected:
    void paintEvent(QPaintEvent *event) Q_DECL_OVERRIDE;

    void updateTileImages();
    int _memoryOffset;
    bool _multicolor;
    ImportVICEDialog* _parentDialog;    // weak ref
};

#endif // IMPORTVICESCREENRAMWIDGET_H
