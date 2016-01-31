#ifndef SELECTCOLORDIALOG_H
#define SELECTCOLORDIALOG_H

#include <QDialog>

namespace Ui {
class SelectColorDialog;
}

class ColorRectWidget;

class SelectColorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SelectColorDialog(QWidget *parent = 0);
    ~SelectColorDialog();

    void setCurrentColor(int colorIndex);
    int getSelectedColor() const;

signals:
    void colorSelected(int colorIndex);

protected:
    void mousePressEvent(QMouseEvent* event) Q_DECL_OVERRIDE;

private:
    Ui::SelectColorDialog *ui;

    int _selectedColor;
    ColorRectWidget* _widgets[16];
};

#endif // SELECTCOLORDIALOG_H
