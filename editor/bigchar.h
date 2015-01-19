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

public slots:
    void animate();

protected:
    void paintEvent(QPaintEvent *event) Q_DECL_OVERRIDE;

private:
    int elapsed;
};
//! [0]

