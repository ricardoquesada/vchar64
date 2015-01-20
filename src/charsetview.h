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

signals:
    void charSelected(int index);

protected:
    void paintEvent(QPaintEvent *event) Q_DECL_OVERRIDE;
    void mousePressEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
};
//! [0]

