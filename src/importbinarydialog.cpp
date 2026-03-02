#include "importbinarydialog.h"

#include <QDebug>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "mainwindow.h"
#include "preferences.h"
#include "state.h"

ImportBinaryDialog::ImportBinaryDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Import Binary Files"));

    auto mainLayout = new QVBoxLayout(this);
    auto formLayout = new QFormLayout();

    auto createRow = [this, formLayout](const QString& labelText, QLineEdit*& lineEdit, QPushButton*& button, const char* slot) {
        auto hLayout = new QHBoxLayout();
        lineEdit = new QLineEdit(this);
        button = new QPushButton(tr("Browse..."), this);
        hLayout->addWidget(lineEdit);
        hLayout->addWidget(button);
        formLayout->addRow(new QLabel(labelText, this), hLayout);

        connect(button, SIGNAL(clicked()), this, slot);
        connect(lineEdit, &QLineEdit::textChanged, this, &ImportBinaryDialog::onLineEditsChanged);
    };

    QPushButton* btnCharset = nullptr;
    QPushButton* btnMap = nullptr;
    QPushButton* btnColors = nullptr;

    createRow(tr("Charset File:"), _lineEditCharset, btnCharset, SLOT(onBrowseCharset()));
    createRow(tr("Tile Map File:"), _lineEditMap, btnMap, SLOT(onBrowseMap()));
    createRow(tr("Tile Colors File (Optional):"), _lineEditColors, btnColors, SLOT(onBrowseColors()));

    mainLayout->addLayout(formLayout);

    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, this);
    mainLayout->addWidget(buttonBox);

    _btnImport = buttonBox->button(QDialogButtonBox::Ok);
    _btnImport->setText(tr("Import"));

    connect(buttonBox, &QDialogButtonBox::accepted, this, &ImportBinaryDialog::onImport);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    updateImportButtonState();
}

ImportBinaryDialog::~ImportBinaryDialog() = default;

void ImportBinaryDialog::onBrowseCharset()
{
    QString filter = tr("All files (*.*)");
    QString fn = QFileDialog::getOpenFileName(this, tr("Select Charset File"), Preferences::getInstance().getLastUsedDirectory(), filter);
    if (!fn.isEmpty()) {
        _lineEditCharset->setText(fn);
    }
}

void ImportBinaryDialog::onBrowseMap()
{
    QString filter = tr("All files (*.*)");
    QString fn = QFileDialog::getOpenFileName(this, tr("Select Tile Map File"), Preferences::getInstance().getLastUsedDirectory(), filter);
    if (!fn.isEmpty()) {
        _lineEditMap->setText(fn);
    }
}

void ImportBinaryDialog::onBrowseColors()
{
    QString filter = tr("All files (*.*)");
    QString fn = QFileDialog::getOpenFileName(this, tr("Select Tile Colors File"), Preferences::getInstance().getLastUsedDirectory(), filter);
    if (!fn.isEmpty()) {
        _lineEditColors->setText(fn);
    }
}

void ImportBinaryDialog::onLineEditsChanged()
{
    updateImportButtonState();
}

void ImportBinaryDialog::updateImportButtonState()
{
    bool canImport = !_lineEditCharset->text().isEmpty() && !_lineEditMap->text().isEmpty();
    _btnImport->setEnabled(canImport);
}

void ImportBinaryDialog::onImport()
{
    QFile charsetFile(_lineEditCharset->text());
    if (!charsetFile.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, tr("Error"), tr("Could not open charset file."));
        return;
    }
    QByteArray charsetData = charsetFile.readAll();

    QFile mapFile(_lineEditMap->text());
    if (!mapFile.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, tr("Error"), tr("Could not open map file."));
        return;
    }
    QByteArray mapData = mapFile.readAll();

    QByteArray colorsData;
    if (!_lineEditColors->text().isEmpty()) {
        QFile colorsFile(_lineEditColors->text());
        if (!colorsFile.open(QIODevice::ReadOnly)) {
            QMessageBox::critical(this, tr("Error"), tr("Could not open colors file."));
            return;
        }
        colorsData = colorsFile.readAll();
    }

    QFileInfo info(_lineEditCharset->text());
    Preferences::getInstance().setLastUsedDirectory(info.absolutePath());

    // Guess map size
    int size = mapData.size();
    QSize mapSize(40, std::max(1, size / 40));

    auto state = new State(info.filePath(), mapSize);

    // Copy charset
    int copyChars = std::min((int)charsetData.size(), State::CHAR_BUFFER_SIZE);
    std::copy(charsetData.begin(), charsetData.begin() + copyChars, std::begin(state->getCharsetBuffer()));

    // Copy map
    auto& targetMap = state->getMapBuffer();
    targetMap.resize(size);
    std::copy(mapData.begin(), mapData.end(), targetMap.begin());

    // Copy colors
    auto& targetColors = state->getTileColors();
    if (colorsData.size() > 0) {
        int copyColors = std::min((int)colorsData.size(), State::TILE_COLORS_BUFFER_SIZE);
        std::copy(colorsData.begin(), colorsData.begin() + copyColors, std::begin(targetColors));
    } else {
        std::fill(std::begin(targetColors), std::end(targetColors), State::TILE_COLORS_DEFAULT);
    }

    state->setMulticolorMode(false);
    state->setForegroundColorMode(State::FOREGROUND_COLOR_PER_TILE);

    MainWindow::getInstance()->createDocument(state);
    accept();
}
