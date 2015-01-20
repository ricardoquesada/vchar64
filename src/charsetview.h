/****************************************************************************

****************************************************************************/

#pragma once

#include <QWidget>

//! [0]

class CharSetView : public QWidget
{
    Q_OBJECT

public:
    CharSetView(QWidget *parent);

protected:
    void paintEvent(QPaintEvent *event) Q_DECL_OVERRIDE;
    void mousePressEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
};
//! [0]

