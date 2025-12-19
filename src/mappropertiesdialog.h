#pragma once
#include <QDialog>

class State;

namespace Ui {
class MapPropertiesDialog;
}

class MapPropertiesDialog : public QDialog {
    Q_OBJECT

public:
    explicit MapPropertiesDialog(State* state, QWidget* parent = nullptr);
    virtual ~MapPropertiesDialog() override;

private slots:
    void on_buttonBox_accepted();

private:
    Ui::MapPropertiesDialog* ui;

    State* _state;
};
