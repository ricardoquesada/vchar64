/****************************************************************************

****************************************************************************/

#pragma once

#include <QWidget>

//! [0]

class BigChar : public QWidget
{
    Q_OBJECT

public:
    BigChar(QWidget *parent);

protected:
    void paintEvent(QPaintEvent *event) Q_DECL_OVERRIDE;

};
//! [0]

