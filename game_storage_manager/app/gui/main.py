from __future__ import annotations

import sys

from PySide6.QtWidgets import QApplication

from game_storage_manager.ui.views.main_window import MainWindow
from game_storage_manager.utils import get_resource_path


def main():
    app = QApplication(sys.argv)
    QApplication.setApplicationName("Game Storage Manager")
    QApplication.setOrganizationName("Game Storage Manager")

    qss_path = get_resource_path("game_storage_manager/resources/theme_dark.qss")
    try:
        with open(qss_path, "r", encoding="utf-8") as f:
            app.setStyleSheet(f.read())
    except OSError:
        pass

    window = MainWindow()
    window.resize(1120, 680)
    window.show()

    sys.exit(app.exec())


if __name__ == "__main__":
    main()
