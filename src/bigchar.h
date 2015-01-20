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
    void setIndex(int index);

signals:
    void indexChanged(int index);

protected:
    void paintEvent(QPaintEvent *event) Q_DECL_OVERRIDE;

    int _index;
};
//! [0]

