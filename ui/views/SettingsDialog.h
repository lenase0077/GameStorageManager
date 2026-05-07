#pragma once

#include <QDialog>
#include <QComboBox>
#include <QPushButton>

namespace gsm::ui {

class SettingsDialog : public QDialog {
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    int defaultAlgorithmIndex() const;

private:
    void loadSettings();
    void saveSettings();

    QComboBox* algorithmCombo_ = nullptr;
    QPushButton* saveButton_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
};

} // namespace gsm::ui
