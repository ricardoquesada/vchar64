#include "aboutdialog.h"
#include "ui_aboutdialog.h"

AboutDialog::AboutDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AboutDialog)
{
    ui->setupUi(this);

    const QString html = QCoreApplication::translate(
            "AboutDialog",
            "<p align=\"center\"><font size=\"+2\"><b>VChar64 Editor</b></font><br><i>Version %1</i></p>\n"
            "<p align=\"center\">Copyright 2015 Ricardo Quesada<br>(see the AUTHORS file for a full list of contributors)</p>\n"
            "<p align=\"center\">You may modify and redistribute this program under the terms of the <a href=\"http://www.apache.org/licenses/LICENSE-2.0\">Apache License v2.0.<a/>"
            "<p align=\"center\"><a href=\"http://towp8.com/\">http://towp8.com/</a></p>\n")
            .arg(QApplication::applicationVersion());

    ui->textBrowser->setHtml(html);
}

AboutDialog::~AboutDialog()
{
    delete ui;
}
