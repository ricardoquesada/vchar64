#pragma once

#include <QDialog>
#include <QString>

class QLineEdit;
class QPushButton;

class ImportBinaryDialog : public QDialog {
    Q_OBJECT

public:
    explicit ImportBinaryDialog(QWidget* parent = nullptr);
    ~ImportBinaryDialog() override;

private slots:
    void onBrowseCharset();
    void onBrowseMap();
    void onBrowseColors();
    void onImport();
    void onLineEditsChanged();

private:
    void updateImportButtonState();

    QLineEdit* _lineEditCharset;
    QLineEdit* _lineEditMap;
    QLineEdit* _lineEditColors;
    QPushButton* _btnImport;
};
