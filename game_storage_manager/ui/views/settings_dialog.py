from __future__ import annotations

from PySide6.QtCore import QSettings
from PySide6.QtWidgets import (
    QComboBox,
    QDialog,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QVBoxLayout,
)


class SettingsDialog(QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Settings")
        self.setModal(True)
        self.setFixedSize(350, 150)

        root_layout = QVBoxLayout(self)
        root_layout.setContentsMargins(20, 20, 20, 20)
        root_layout.setSpacing(15)

        algo_layout = QHBoxLayout()
        algo_label = QLabel("Default Compression Profile:", self)
        self._algorithm_combo = QComboBox(self)
        self._algorithm_combo.addItem("Fast (XPRESS4K)")
        self._algorithm_combo.addItem("Balanced (XPRESS8K)")
        self._algorithm_combo.addItem("Strong (XPRESS16K)")
        self._algorithm_combo.addItem("Max (LZX)")

        algo_layout.addWidget(algo_label)
        algo_layout.addWidget(self._algorithm_combo, 1)
        root_layout.addLayout(algo_layout)

        root_layout.addStretch(1)

        buttons_layout = QHBoxLayout()
        buttons_layout.addStretch(1)
        save_button = QPushButton("Save", self)
        cancel_button = QPushButton("Cancel", self)
        buttons_layout.addWidget(cancel_button)
        buttons_layout.addWidget(save_button)
        root_layout.addLayout(buttons_layout)

        save_button.clicked.connect(self._save_and_accept)
        cancel_button.clicked.connect(self.reject)

        self._load_settings()

    def default_algorithm_index(self) -> int:
        return self._algorithm_combo.currentIndex()

    def _load_settings(self):
        settings = QSettings("GameStorageManager", "App")
        algo_index = settings.value("defaultAlgorithm", 1)
        try:
            algo_index = int(algo_index)
        except (TypeError, ValueError):
            algo_index = 1
        if 0 <= algo_index < self._algorithm_combo.count():
            self._algorithm_combo.setCurrentIndex(algo_index)

    def _save_and_accept(self):
        settings = QSettings("GameStorageManager", "App")
        settings.setValue("defaultAlgorithm", self._algorithm_combo.currentIndex())
        self.accept()
