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

#include "aboutdialog.h"
#include "ui_aboutdialog.h"


AboutDialog::AboutDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AboutDialog)
{
    ui->setupUi(this);

    const char *htmlraw = R"html(
<p align="center"><img height="92" width="92" src=":/res/logo512.png"></p>
<p align="center"><font size="+2"><b>VChar64</b></font><br><i>Version %1</i></p>
<p align="center">Copyright 2015 Ricardo Quesada<br>(see the <a href="https://github.com/ricardoquesada/vchar64/blob/master/AUTHORS">AUTHORS</a> file for a full list of contributors)</p>
<p align="center">You may modify and redistribute this program under the terms of the <a href="http://www.apache.org/licenses/LICENSE-2.0">Apache License v2.0.<a/>
<p align="center"><a href="https://github.com/ricardoquesada/vchar64/">https://github.com/ricardoquesada/vchar64</a></p>
)html";

    const QString html = QCoreApplication::translate(
            "AboutDialog",
                htmlraw)
            .arg(QApplication::applicationVersion());

    ui->textBrowser->setHtml(html);
}

AboutDialog::~AboutDialog()
{
    delete ui;
}
