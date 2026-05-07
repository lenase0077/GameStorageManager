#include "ui/views/SettingsDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QVariant>

namespace gsm::ui {

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Settings");
    setModal(true);
    setFixedSize(350, 150);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(20, 20, 20, 20);
    rootLayout->setSpacing(15);

    auto* algoLayout = new QHBoxLayout();
    auto* algoLabel = new QLabel("Default Compression Profile:", this);
    algorithmCombo_ = new QComboBox(this);
    algorithmCombo_->addItem("Fast (XPRESS4K)");
    algorithmCombo_->addItem("Balanced (XPRESS8K)");
    algorithmCombo_->addItem("Strong (XPRESS16K)");
    algorithmCombo_->addItem("Max (LZX)");
    
    algoLayout->addWidget(algoLabel);
    algoLayout->addWidget(algorithmCombo_, 1);
    rootLayout->addLayout(algoLayout);

    rootLayout->addStretch(1);

    auto* buttonsLayout = new QHBoxLayout();
    buttonsLayout->addStretch(1);
    saveButton_ = new QPushButton("Save", this);
    cancelButton_ = new QPushButton("Cancel", this);
    buttonsLayout->addWidget(cancelButton_);
    buttonsLayout->addWidget(saveButton_);
    rootLayout->addLayout(buttonsLayout);

    connect(saveButton_, &QPushButton::clicked, this, [this]() {
        saveSettings();
        accept();
    });

    connect(cancelButton_, &QPushButton::clicked, this, &QDialog::reject);

    loadSettings();
}

int SettingsDialog::defaultAlgorithmIndex() const
{
    return algorithmCombo_->currentIndex();
}

void SettingsDialog::loadSettings()
{
    QSettings settings("GameStorageManager", "App");
    int algoIndex = settings.value("defaultAlgorithm", 1).toInt(); // Default to Balanced (1)
    if (algoIndex >= 0 && algoIndex < algorithmCombo_->count()) {
        algorithmCombo_->setCurrentIndex(algoIndex);
    }
}

void SettingsDialog::saveSettings()
{
    QSettings settings("GameStorageManager", "App");
    settings.setValue("defaultAlgorithm", algorithmCombo_->currentIndex());
}

} // namespace gsm::ui
