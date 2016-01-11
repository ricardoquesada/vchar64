/****************************************************************************
Copyright 2016 Ricardo Quesada

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

#include "importkoaladialog.h"
#include "ui_importkoaladialog.h"

#include <QFileDialog>
#include <QSettings>

ImportKoalaDialog::ImportKoalaDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ImportKoalaDialog)
{
    ui->setupUi(this);

    auto lastDir = QSettings("RetroMoe","VChar64").value("dir/lastdir").toString();
    ui->lineEdit->setText(lastDir);
}

ImportKoalaDialog::~ImportKoalaDialog()
{
    delete ui;
}

void ImportKoalaDialog::on_pushButton_clicked()
{
    auto filter = tr("Koala files");
    auto fn = QFileDialog::getOpenFileName(this,
                                           tr("Select Koala File"),
                                           ui->lineEdit->text(),
                                           tr(
                                               "All files (*);;" \
                                               "Koala files (*.koa *.kla);;"
                                           ),
                                           &filter
                                           /*,QFileDialog::DontUseNativeDialog*/
                                           );

    if (fn.length()> 0)
    {
        if (fn != _filepath)
        {
            _filepath = fn;
            ui->lineEdit->setText(fn);

            _validKoalaFile = validateKoalaFile(fn);
        }
    }
}


// helpers


bool ImportKoalaDialog::validateKoalaFile(const QString& filepath)
{
    QFileInfo info(filepath);

    if (info.exists() && info.isFile() && (info.size() == 10003 || info.size() == 10002))
    {
        ui->widgetKoala->loadKoala(filepath);
        return true;
    }
    return false;
}

bool ImportKoalaDialog::processChardef(const std::string& key, quint8* outKey, quint8* outColorRAM)
{
    // colorRAM will be the color >=8 not in d02x that is the more used one.
    quint8 colorRam[8];

    memset(colorRam, 0, sizeof(colorRam));

    for (int i=0; i<8; ++i)
    {
        quint8 bits = 0;

        for (int j=0; j<4; ++j)
        {
            char c = key[i*4+j];
            int colorIndex = c - '0';
            if (colorIndex > 9)
                colorIndex -= 7;

            if (colorIndex == ui->widgetKoala->_d02xColors[0])
                /* nothing, bits = 0 */
                ;
            else if (colorIndex == ui->widgetKoala->_d02xColors[1])
                bits |= (1 << (6-(j*2)));
            else if (colorIndex == ui->widgetKoala->_d02xColors[2])
                bits |= (2 << (6-(j*2)));
            else if (colorIndex >= 8)
            {
                bits |= (3 << (6-(j*2)));
                colorRam[colorIndex-8]++;
            }
            else
            {
                // invalid char... convert it.
            }
        }

        outKey[i] = bits;
    }
    std::sort(std::begin(colorRam), std::end(colorRam));
    *outColorRAM = colorRam[7];

    return true;
}

void ImportKoalaDialog::on_pushButtonConvert_clicked()
{
    auto orig = ui->widgetKoala;
    auto conv = ui->widgetCharset;

    if (ui->radioMostUsedColors->isChecked())
        orig->strategyHiColorsUseFixedColors();
    else
        orig->strategyMostUsedColorsUseFixedColors();
    orig->reportResults();


    for (int i=0; i<3; ++i)
        conv->_d02x[i] = orig->_d02xColors[i];

    int charsetCount = 0;

    for (auto it = orig->_uniqueChars.begin(); it != orig->_uniqueChars.end(); ++it)
    {
        quint8 chardef[8];
        memset(chardef, 0, sizeof(chardef));

        quint8 colorRAM;
        processChardef(it->first, chardef, &colorRAM);

        conv->populateScreenAndColorRAM(it->second, charsetCount, colorRAM);
        conv->setCharset(charsetCount, chardef);

        charsetCount++;
    }

    conv->update();
}
