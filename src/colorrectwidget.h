/****************************************************************************
Copyright 2015 Ricardo Quesada

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
****************************************************************************/

#pragma once

#include <QWidget>

class ColorRectWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ColorRectWidget(QWidget *parent = 0);
    ~ColorRectWidget();

    void setPen(int pen);
    int getPen() const;

signals:

public slots:

protected:
    void paintEvent(QPaintEvent *event);


    int _pen;
};

