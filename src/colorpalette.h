/****************************************************************************

****************************************************************************/

#pragma once

#include <QWidget>

//! [0]

class ColorPalette : public QWidget
{
    Q_OBJECT

public:
    ColorPalette(QWidget *parent);


protected:
    void paintEvent(QPaintEvent *event) Q_DECL_OVERRIDE;
    void mousePressEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
};
//! [0]

