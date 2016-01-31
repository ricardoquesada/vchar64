#include "selectcolordialog.h"
#include "ui_selectcolordialog.h"

#include <QMouseEvent>

SelectColorDialog::SelectColorDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::SelectColorDialog)
    , _selectedColor(-1)
{
    ui->setupUi(this);

    _widgets[0] = ui->widget;
    _widgets[1] = ui->widget_2;
    _widgets[2] = ui->widget_3;
    _widgets[3] = ui->widget_4;
    _widgets[4] = ui->widget_5;
    _widgets[5] = ui->widget_6;
    _widgets[6] = ui->widget_7;
    _widgets[7] = ui->widget_8;
    _widgets[8] = ui->widget_9;
    _widgets[9] = ui->widget_10;
    _widgets[10] = ui->widget_11;
    _widgets[11] = ui->widget_12;
    _widgets[12] = ui->widget_13;
    _widgets[13] = ui->widget_14;
    _widgets[14] = ui->widget_15;
    _widgets[15] = ui->widget_16;

    const int TOTAL_WIDGETS = sizeof(_widgets) / sizeof(_widgets[0]);

    for (int i=0; i<TOTAL_WIDGETS; ++i)
        _widgets[i]->setColorIndex(i);

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &SelectColorDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this,  &SelectColorDialog::reject);
}

int SelectColorDialog::getSelectedColor() const
{
    return _selectedColor;
}

void SelectColorDialog::setCurrentColor(int colorIndex)
{
    if (_selectedColor != colorIndex)
    {
        if (_selectedColor != -1)
            _widgets[_selectedColor]->setSelected(false);

        _selectedColor = colorIndex;
        _widgets[_selectedColor]->setSelected(true);

        emit colorSelected(colorIndex);
    }
}

SelectColorDialog::~SelectColorDialog()
{
    delete ui;
}

void SelectColorDialog::mousePressEvent(QMouseEvent* event)
{
    for (int i=0; i<16; i++)
    {
        auto localPos = _widgets[i]->mapFromParent(event->pos());
        if (_widgets[i]->rect().contains(localPos))
        {
            setCurrentColor(i);
            event->accept();
            return;
        }
    }
    event->ignore();
}
