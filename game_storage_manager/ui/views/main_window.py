from __future__ import annotations

import json
import os
import shutil
from pathlib import Path

from PySide6.QtCore import QObject, Qt, QTimer, Signal
from PySide6.QtGui import QColor, QIcon
from PySide6.QtWidgets import (
    QComboBox,
    QFileDialog,
    QFrame,
    QGraphicsDropShadowEffect,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QMainWindow,
    QMessageBox,
    QProgressBar,
    QPushButton,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from game_storage_manager.core.analyzer.game_analysis import GameAnalysis
from game_storage_manager.core.compressor.compression_task import CompressionResult
from game_storage_manager.core.rules_engine.recommendation_engine import (
    CompressionAlgorithm,
    CompressionRecommendation,
    RecommendationAction,
    RecommendationEngine,
    algorithm_to_string,
    risk_to_string,
)
from game_storage_manager.core.safety.safety_metadata import SafetyMetadata, SafetyOperationState
from game_storage_manager.core.safety.safety_metadata_store import (
    SafetyMetadataStore,
)
from game_storage_manager.core.scanner.game_entry import GameEntry, GameSource, game_source_to_string, parse_game_source
from game_storage_manager.system.filesystem.filesystem import join_path, normalize_path
from game_storage_manager.ui.controllers.analysis_controller import AnalysisController
from game_storage_manager.ui.controllers.compression_controller import CompressionController
from game_storage_manager.ui.views.settings_dialog import SettingsDialog
from game_storage_manager.utils import get_resource_path


def _format_bytes(b: int) -> str:
    kib = 1024.0
    mib = kib * 1024.0
    gib = mib * 1024.0
    if b >= gib:
        return f"{b / gib:.2f} GB"
    if b >= mib:
        return f"{b / mib:.2f} MB"
    if b >= kib:
        return f"{b / kib:.2f} KB"
    return f"{b} B"


def _base_name_from_path(path: str) -> str:
    normalized = path.replace("\\", "/").rstrip("/")
    sep = normalized.rfind("/")
    return normalized[sep + 1:] if sep >= 0 else normalized


def _recommendation_text(rec: CompressionRecommendation) -> str:
    if rec.action == RecommendationAction.Skip:
        return "Skip"
    if rec.algorithm is None:
        return "Pending"
    return algorithm_to_string(rec.algorithm)


def _reason_text(rec: CompressionRecommendation) -> str:
    return ", ".join(rec.reasons)


_K_DRIVE_HEADER = Qt.ItemDataRole.UserRole + 1
_K_PATH = Qt.ItemDataRole.UserRole


class _AsyncWatcher(QObject):
    finished = Signal(object)

    def __init__(self, future, parent=None):
        super().__init__(parent)
        self._future = future
        self._timer = QTimer(self)
        self._timer.timeout.connect(self._check)
        self._timer.start(50)

    def _check(self):
        if self._future.done():
            self._timer.stop()
            try:
                self.finished.emit(self._future.result())
            except Exception as e:
                self.finished.emit(e)

    def set_future(self, future):
        self._future = future
        if not self._timer.isActive():
            self._timer.start(50)


class MainWindow(QMainWindow):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._selected_folder = ""
        self._analyzing_row = -1
        self._active_row = -1
        self._pending_analysis_rows: list[int] = []
        self._active_analysis: GameAnalysis | None = None
        self._active_recommendation: CompressionRecommendation | None = None
        self._library_games: list[GameEntry] = []
        self._analysis_controller = AnalysisController()
        self._compression_controller = CompressionController()
        self._current_algorithm = CompressionAlgorithm.Xpress8k
        self._row_analyses: dict[int, GameAnalysis] = {}
        self._row_recommendations: dict[int, CompressionRecommendation] = {}

        self._analysis_watcher: _AsyncWatcher | None = None
        self._steam_scan_watcher: _AsyncWatcher | None = None
        self._compress_watcher: _AsyncWatcher | None = None
        self._restore_watcher: _AsyncWatcher | None = None

        self._build_layout()
        self._apply_theme()
        self._connect_signals()
        self._set_busy(False)
        self._load_library()

    def _build_layout(self):
        central = QWidget(self)
        root_layout = QVBoxLayout(central)
        root_layout.setContentsMargins(24, 20, 24, 20)
        root_layout.setSpacing(14)

        header_layout = QHBoxLayout()
        title_block = QVBoxLayout()

        title_label = QLabel("Game Storage Manager", central)
        title_label.setObjectName("titleLabel")
        self._status_label = QLabel("Ready", central)
        self._status_label.setObjectName("statusLabel")

        title_block.addWidget(title_label)
        title_block.addWidget(self._status_label)
        header_layout.addLayout(title_block, 1)

        metrics_frame = QFrame(central)
        metrics_frame.setObjectName("metricsFrame")
        shadow = QGraphicsDropShadowEffect(metrics_frame)
        shadow.setBlurRadius(15)
        shadow.setColor(QColor(0, 0, 0, 80))
        shadow.setOffset(0, 4)
        metrics_frame.setGraphicsEffect(shadow)

        metrics_layout = QHBoxLayout(metrics_frame)
        metrics_layout.setContentsMargins(16, 8, 16, 8)
        metrics_layout.setSpacing(20)

        self._games_count_label = QLabel("\U0001f3ae 0 Optimized", metrics_frame)
        self._games_count_label.setObjectName("metricValue")
        self._space_saved_label = QLabel("\U0001f525 0 B Saved", metrics_frame)
        self._space_saved_label.setObjectName("metricValue")
        self._ratio_label = QLabel("\U0001f4ca 0.0% Avg Ratio", metrics_frame)
        self._ratio_label.setObjectName("metricValue")

        metrics_layout.addWidget(self._games_count_label)
        metrics_layout.addWidget(self._space_saved_label)
        metrics_layout.addWidget(self._ratio_label)
        header_layout.addWidget(metrics_frame, 0, Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
        root_layout.addLayout(header_layout)

        action_frame = QFrame(central)
        action_frame.setObjectName("toolbarFrame")
        action_shadow = QGraphicsDropShadowEffect(action_frame)
        action_shadow.setBlurRadius(15)
        action_shadow.setColor(QColor(0, 0, 0, 80))
        action_shadow.setOffset(0, 4)
        action_frame.setGraphicsEffect(action_shadow)

        action_layout = QHBoxLayout(action_frame)
        action_layout.setContentsMargins(14, 12, 14, 12)
        action_layout.setSpacing(10)

        res_dir = get_resource_path("game_storage_manager/resources")

        def _btn(svg, label):
            return QPushButton(QIcon(str(res_dir / svg)), label, action_frame)

        self._settings_button = _btn("settings.svg", " Settings")
        self._select_folder_button = _btn("folder-open.svg", " Add Game Folder")
        self._scan_steam_button = _btn("gamepad.svg", " Scan Steam")
        self._analyze_selected_button = _btn("search.svg", " Analyze Selected")
        self._analyze_all_button = _btn("folder-search.svg", " Analyze All")
        self._optimize_button = _btn("zap.svg", " Optimize")
        self._restore_button = _btn("undo.svg", " Restore")
        self._remove_button = _btn("trash.svg", " Remove")
        self._cancel_button = _btn("stop.svg", " Cancel")
        self._selected_folder_label = QLabel("No folder selected", action_frame)
        self._selected_folder_label.setObjectName("pathLabel")

        self._optimize_button.setEnabled(False)
        self._restore_button.setEnabled(False)
        self._cancel_button.setEnabled(False)

        self._profile_combo = QComboBox(action_frame)
        self._profile_combo.addItem("Fast (XPRESS4K)")
        self._profile_combo.addItem("Balanced (XPRESS8K)")
        self._profile_combo.addItem("Strong (XPRESS16K)")
        self._profile_combo.addItem("Max (LZX)")

        from PySide6.QtCore import QSettings
        settings = QSettings("GameStorageManager", "App")
        default_algo = settings.value("defaultAlgorithm", 1)
        try:
            default_algo = int(default_algo)
        except (TypeError, ValueError):
            default_algo = 1
        if 0 <= default_algo < self._profile_combo.count():
            self._profile_combo.setCurrentIndex(default_algo)

        for w in [
            self._settings_button, self._select_folder_button, self._scan_steam_button,
            self._analyze_selected_button, self._analyze_all_button, self._optimize_button,
            self._restore_button, self._remove_button, self._cancel_button,
            self._profile_combo, self._selected_folder_label,
        ]:
            action_layout.addWidget(w, 1 if w is self._selected_folder_label else 0)

        root_layout.addWidget(action_frame)

        self._games_table = QTableWidget(0, 8, central)
        self._games_table.setHorizontalHeaderLabels([
            "Game", "Path", "Size", "Files", "Compressed assets",
            "Recommendation", "Risk", "Status",
        ])
        self._games_table.verticalHeader().setVisible(False)
        self._games_table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        self._games_table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self._games_table.setAlternatingRowColors(True)
        self._games_table.horizontalHeader().setStretchLastSection(True)
        self._games_table.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeMode.Interactive)
        self._games_table.setColumnWidth(0, 200)
        self._games_table.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
        root_layout.addWidget(self._games_table, 1)

        self._progress_bar = QProgressBar(central)
        self._progress_bar.setRange(0, 1)
        self._progress_bar.setValue(0)
        self._progress_bar.setTextVisible(False)
        root_layout.addWidget(self._progress_bar)

        self.setCentralWidget(central)

    def _apply_theme(self):
        qss_path = get_resource_path("game_storage_manager/resources/theme_dark.qss")
        try:
            with open(qss_path, "r", encoding="utf-8") as f:
                self.setStyleSheet(f.read())
        except OSError:
            self.setStyleSheet(
                'QMainWindow, QWidget { background: #111315; color: #E9EDF1;'
                ' font-family: "Segoe UI"; font-size: 10pt; }'
                " QLabel#titleLabel { font-size: 20pt; font-weight: 700; color: #F6F8FA; }"
                " QLabel#statusLabel, QLabel#pathLabel { color: #9BA6B2; }"
                " QLabel#metricValue { color: #8EE6B1; font-size: 11pt; font-weight: 700; }"
                " QFrame#metricsFrame { background: #16241D; border: 1px solid #2B4D36;"
                " border-radius: 8px; }"
                " QFrame#toolbarFrame { background: #1A1D21; border: 1px solid #2B3138;"
                " border-radius: 6px; }"
                " QPushButton { background: #242A31; border: 1px solid #39414B;"
                " border-radius: 5px; color: #F4F7FA; min-height: 30px; padding: 0 14px; }"
                " QPushButton:hover { background: #2C3440; border-color: #4D9DE0; }"
                " QPushButton:disabled { color: #66717D; background: #181B1F;"
                " border-color: #2A3037; }"
                " QComboBox { background: #242A31; border: 1px solid #39414B;"
                " border-radius: 5px; color: #F4F7FA; min-height: 30px; padding: 0 14px; }"
                " QComboBox:hover { border-color: #4D9DE0; }"
                " QComboBox QAbstractItemView { background: #1A1D21;"
                " border: 1px solid #39414B; color: #F4F7FA;"
                " selection-background-color: #24425C; }"
                " QComboBox::drop-down { border: 0; width: 24px; }"
                " QTableWidget { background: #15181C;"
                " alternate-background-color: #181C21;"
                " border: 1px solid #2B3138; border-radius: 6px;"
                " gridline-color: #283039;"
                " selection-background-color: #24425C;"
                " selection-color: #FFFFFF; }"
                " QHeaderView::section { background: #20252B; color: #B8C0CA;"
                " border: 0; border-right: 1px solid #303842;"
                " padding: 8px; font-weight: 600; }"
                " QProgressBar { background: #1A1D21; border: 1px solid #2B3138;"
                " border-radius: 4px; min-height: 8px; max-height: 8px; }"
                " QProgressBar::chunk { background: #4D9DE0; border-radius: 4px; }"
            )

    def _connect_signals(self):
        self._select_folder_button.clicked.connect(self._choose_folder)
        self._scan_steam_button.clicked.connect(self._start_steam_scan)
        self._analyze_selected_button.clicked.connect(self._on_analyze_selected)
        self._analyze_all_button.clicked.connect(self._on_analyze_all)
        self._optimize_button.clicked.connect(self._on_optimize)
        self._restore_button.clicked.connect(self._on_restore)
        self._remove_button.clicked.connect(self._on_remove_game)
        self._settings_button.clicked.connect(self._on_settings)
        self._cancel_button.clicked.connect(self._on_cancel)
        self._profile_combo.currentIndexChanged.connect(self._on_profile_changed)

    def _on_cancel(self):
        self._analysis_controller.cancel()
        self._compression_controller.cancel()
        self._pending_analysis_rows.clear()
        self._status_label.setText("Cancelling...")
        self._cancel_button.setEnabled(False)

    def _choose_folder(self):
        folder = QFileDialog.getExistingDirectory(self, "Select Game Folder")
        if not folder:
            return

        self._selected_folder = folder
        self._selected_folder_label.setText(folder)

        exists = any(
            g.install_path.lower() == folder.lower()
            for g in self._library_games
        )

        if not exists:
            entry = GameEntry()
            entry.name = _base_name_from_path(folder)
            entry.install_path = folder
            entry.source = GameSource.Manual
            self._library_games.append(entry)
            self._save_library()
            self._refresh_table_view()

        self._active_row = -1
        self._active_analysis = None
        self._active_recommendation = None
        self._optimize_button.setEnabled(False)
        self._restore_button.setEnabled(False)
        self._start_analysis(folder)

    def _start_analysis(self, folder_path: str, game_name: str = ""):
        self._set_busy(True)
        self._status_label.setText("Analyzing")

        self._analyzing_row = -1
        for r in range(self._games_table.rowCount()):
            item = self._games_table.item(r, 0)
            if item and not item.data(_K_DRIVE_HEADER):
                if item.data(_K_PATH) and item.data(_K_PATH).lower() == folder_path.lower():
                    self._analyzing_row = r
                    break

        if self._analyzing_row >= 0:
            self._update_row_status(self._analyzing_row, "Analyzing...")

        self._selected_folder_label.setText(folder_path)
        future = self._analysis_controller.analyze_folder(folder_path, game_name)
        self._analysis_watcher = _AsyncWatcher(future, self)
        self._analysis_watcher.finished.connect(self._finish_analysis)

    def _start_steam_scan(self):
        self._set_busy(True)
        self._status_label.setText("Scanning Steam")
        self._selected_folder_label.setText("Scanning Steam libraries")
        self._active_row = -1
        self._active_analysis = None
        self._active_recommendation = None
        self._optimize_button.setEnabled(False)
        self._restore_button.setEnabled(False)

        future = self._analysis_controller.scan_steam_games()
        self._steam_scan_watcher = _AsyncWatcher(future, self)
        self._steam_scan_watcher.finished.connect(self._finish_steam_scan)

    def _finish_analysis(self, result):
        self._set_busy(False)

        if isinstance(result, Exception):
            self._status_label.setText("Error")
            self._analyzing_row = -1
            if self._pending_analysis_rows:
                self._process_next_analysis()
            return

        analysis: GameAnalysis = result
        if not analysis.is_valid:
            self._status_label.setText("Error")
            if analysis.error_message != "Analysis cancelled by user.":
                QMessageBox.warning(self, "Analysis failed", analysis.error_message)
            self._analyzing_row = -1
            if self._pending_analysis_rows:
                self._process_next_analysis()
            return

        if self._analyzing_row < 0:
            for r in range(self._games_table.rowCount()):
                item = self._games_table.item(r, 0)
                if item and not item.data(_K_DRIVE_HEADER):
                    if item.data(_K_PATH) and item.data(_K_PATH).lower() == analysis.root_path.lower():
                        self._analyzing_row = r
                        break

        if self._analyzing_row >= 0:
            self._update_game_row(self._analyzing_row, analysis)
            self._active_row = self._analyzing_row
            self._analyzing_row = -1
        else:
            self._show_analysis(analysis)
            self._active_row = self._games_table.rowCount() - 1

        engine = RecommendationEngine()
        rec = engine.recommend_with_algorithm(analysis, self._current_algorithm)
        self._update_active_state(analysis, rec)
        self._status_label.setText("Ready")

        if self._pending_analysis_rows:
            self._process_next_analysis()

    def _finish_steam_scan(self, result):
        self._set_busy(False)

        if isinstance(result, Exception):
            self._status_label.setText("Scan error")
            return

        games: list[GameEntry] = result
        for g in games:
            exists = any(
                lib_g.install_path.lower() == g.install_path.lower()
                for lib_g in self._library_games
            )
            if not exists:
                self._library_games.append(g)

        self._save_library()
        self._refresh_table_view()
        self._apply_stored_metadata()
        self._status_label.setText(f"Steam games found: {len(games)}")

    def _show_analysis(self, analysis: GameAnalysis):
        self._games_table.setUpdatesEnabled(False)
        try:
            engine = RecommendationEngine()
            rec = engine.recommend_with_algorithm(analysis, self._current_algorithm)

            row = self._games_table.rowCount()
            self._games_table.insertRow(row)

            root_path = analysis.root_path
            compressed_assets = f"{analysis.already_compressed_file_count} ext / {analysis.ntfs_compressed_file_count} NTFS"

            source = GameSource.Manual
            for g in self._library_games:
                if g.install_path.lower() == root_path.lower():
                    source = g.source
                    break

            prefix = "\U0001f3ae " if source == GameSource.Steam else "\U0001f4c1 "

            size_text = _format_bytes(analysis.total_bytes)
            status_text = _reason_text(rec)
            is_optimized = False

            if analysis.total_bytes < analysis.logical_bytes and analysis.ntfs_compressed_file_count > 0:
                saved = analysis.logical_bytes - analysis.total_bytes
                if saved > 1024 * 1024:
                    size_text = f"{_format_bytes(analysis.total_bytes)} (-{_format_bytes(saved)})"
                    status_text = f"Optimized ({_format_bytes(saved)} saved)"
                    is_optimized = True

            values = [
                prefix + _base_name_from_path(root_path),
                root_path,
                size_text,
                str(analysis.file_count),
                compressed_assets,
                _recommendation_text(rec),
                risk_to_string(rec.risk),
                status_text,
            ]

            for col, val in enumerate(values):
                item = QTableWidgetItem(val)
                item.setData(_K_PATH, root_path)
                item.setData(_K_DRIVE_HEADER, False)

                if col == 0:
                    if source == GameSource.Steam:
                        item.setForeground(QColor("#4D9DE0"))
                        item.setToolTip("Source: Steam")
                    else:
                        item.setForeground(QColor("#8EE6B1"))
                        item.setToolTip("Source: Manual")

                if col == 7:
                    if is_optimized:
                        item.setForeground(QColor("#8EE6B1"))
                    else:
                        item.setForeground(QColor("#E9EDF1"))

                self._games_table.setItem(row, col, item)

            self._row_analyses[row] = analysis
            self._row_recommendations[row] = rec
        finally:
            self._games_table.setUpdatesEnabled(True)

    def _refresh_table_view(self):
        self._games_table.setUpdatesEnabled(False)
        try:
            self._active_row = -1
            self._active_analysis = None
            self._active_recommendation = None
            self._row_analyses.clear()
            self._row_recommendations.clear()

            sorted_games = sorted(self._library_games, key=lambda g: (
                self._extract_drive_letter(g.install_path),
                g.name,
            ))

            games_per_drive: dict[str, int] = {}
            for game in sorted_games:
                drive = self._extract_drive_letter(game.install_path)
                games_per_drive[drive] = games_per_drive.get(drive, 0) + 1

            self._games_table.clearSpans()
            self._games_table.setRowCount(0)

            current_drive = ""
            total_rows = 0

            for game in sorted_games:
                drive = self._extract_drive_letter(game.install_path)
                if drive != current_drive:
                    current_drive = drive
                    drive_row = total_rows
                    total_rows += 1
                    self._games_table.insertRow(drive_row)

                    label = f"{drive}:  ({games_per_drive.get(drive, 0)} games){self._drive_space_info(drive)}"
                    header_item = QTableWidgetItem(label)
                    header_item.setBackground(QColor("#1E3550"))
                    header_item.setForeground(QColor("#8EC8F2"))
                    bold_font = header_item.font()
                    bold_font.setBold(True)
                    bold_font.setPointSize(bold_font.pointSize() + 1)
                    header_item.setFont(bold_font)
                    header_item.setFlags(Qt.ItemFlag.NoItemFlags)
                    header_item.setData(_K_DRIVE_HEADER, True)
                    self._games_table.setItem(drive_row, 0, header_item)
                    self._games_table.setSpan(drive_row, 0, 1, self._games_table.columnCount())

                game_row = total_rows
                total_rows += 1
                self._games_table.insertRow(game_row)

                path = game.install_path
                folder_exists = Path(path).is_dir()

                prefix = "\U0001f3ae " if game.source == GameSource.Steam else "\U0001f4c1 "
                values = [
                    prefix + game.name,
                    game.install_path,
                    "Not analyzed",
                    "-",
                    "-",
                    "Pending",
                    "-",
                    game_source_to_string(game.source) if folder_exists else "Not found (Missing)",
                ]

                for col, val in enumerate(values):
                    item = QTableWidgetItem(val)
                    item.setData(_K_PATH, path)
                    item.setData(_K_DRIVE_HEADER, False)

                    if col == 0:
                        if game.source == GameSource.Steam:
                            item.setForeground(QColor("#4D9DE0"))
                            item.setToolTip("Source: Steam")
                        else:
                            item.setForeground(QColor("#8EE6B1"))
                            item.setToolTip("Source: Manual")

                    if not folder_exists:
                        item.setForeground(QColor("#F85149"))

                    self._games_table.setItem(game_row, col, item)

            self._analyzing_row = -1
        finally:
            self._games_table.setUpdatesEnabled(True)

    def _get_storage_root(self) -> str:
        local_app_data = os.environ.get("LOCALAPPDATA")
        if local_app_data:
            return join_path(local_app_data, "GameStorageManager/metadata")
        return "metadata"

    def _load_library(self):
        storage_root = self._get_storage_root()
        Path(storage_root).mkdir(parents=True, exist_ok=True)
        lib_path = join_path(storage_root, "library.json")
        try:
            with open(lib_path, "r", encoding="utf-8") as f:
                data = json.load(f)
            if isinstance(data, list):
                self._library_games.clear()
                for obj in data:
                    entry = GameEntry()
                    entry.name = obj.get("name", "")
                    entry.install_path = obj.get("installPath", "")
                    entry.source = parse_game_source(obj.get("source", "manual"))
                    self._library_games.append(entry)
        except (OSError, json.JSONDecodeError):
            pass

        self._refresh_table_view()
        self._apply_stored_metadata()

    def _save_library(self):
        storage_root = self._get_storage_root()
        Path(storage_root).mkdir(parents=True, exist_ok=True)
        lib_path = join_path(storage_root, "library.json")
        data = []
        for g in self._library_games:
            data.append({
                "name": g.name,
                "installPath": g.install_path,
                "source": game_source_to_string(g.source).capitalize() if g.source != GameSource.Manual else "Manual",
            })
        try:
            with open(lib_path, "w", encoding="utf-8") as f:
                json.dump(data, f, indent=2)
        except OSError:
            pass

    def _on_analyze_selected(self):
        current_row = self._games_table.currentRow()
        if current_row < 0 or current_row >= self._games_table.rowCount():
            QMessageBox.information(self, "No selection", "Select a game row first.")
            return

        item = self._games_table.item(current_row, 0)
        if not item:
            return

        if item.data(_K_DRIVE_HEADER):
            QMessageBox.information(self, "Cannot analyze", "Select a game row, not a drive header.")
            return

        game_name = item.text()
        path = item.data(_K_PATH)
        if not path:
            return

        if not Path(path).is_dir():
            QMessageBox.warning(self, "Folder Missing", "The game folder no longer exists.")
            self._update_row_status(current_row, "Not found (Missing)")
            return

        self._analyzing_row = current_row
        self._start_analysis(path, game_name)

    def _on_analyze_all(self):
        self._pending_analysis_rows.clear()
        for r in range(self._games_table.rowCount()):
            item = self._games_table.item(r, 0)
            if item and not item.data(_K_DRIVE_HEADER):
                self._pending_analysis_rows.append(r)

        if self._pending_analysis_rows:
            self._process_next_analysis()
        else:
            QMessageBox.information(self, "Analyze All", "No games to analyze in the library.")

    def _process_next_analysis(self):
        if not self._pending_analysis_rows:
            self._status_label.setText("Ready")
            return

        row = self._pending_analysis_rows.pop(0)
        item = self._games_table.item(row, 0)
        if not item:
            self._process_next_analysis()
            return

        game_name = item.text()
        path = item.data(_K_PATH)

        if not Path(path).is_dir():
            self._update_row_status(row, "Not found (Missing)")
            self._process_next_analysis()
            return

        self._games_table.scrollToItem(item)
        self._games_table.selectRow(row)
        self._active_row = row
        self._analyzing_row = row
        self._start_analysis(path, game_name)

    def _update_game_row(self, row: int, analysis: GameAnalysis):
        self._games_table.setUpdatesEnabled(False)
        try:
            engine = RecommendationEngine()
            rec = engine.recommend_with_algorithm(analysis, self._current_algorithm)

            compressed_assets = f"{analysis.already_compressed_file_count} ext / {analysis.ntfs_compressed_file_count} NTFS"
            path = analysis.root_path

            source = GameSource.Manual
            for g in self._library_games:
                if g.install_path.lower() == path.lower():
                    source = g.source
                    break

            prefix = "\U0001f3ae " if source == GameSource.Steam else "\U0001f4c1 "

            size_text = _format_bytes(analysis.total_bytes)
            status_text = _reason_text(rec)
            is_optimized = False

            if analysis.total_bytes < analysis.logical_bytes and analysis.ntfs_compressed_file_count > 0:
                saved = analysis.logical_bytes - analysis.total_bytes
                if saved > 1024 * 1024:
                    size_text = f"{_format_bytes(analysis.total_bytes)} (-{_format_bytes(saved)})"
                    status_text = f"Optimized ({_format_bytes(saved)} saved)"
                    is_optimized = True

            values = [
                prefix + _base_name_from_path(path),
                path,
                size_text,
                str(analysis.file_count),
                compressed_assets,
                _recommendation_text(rec),
                risk_to_string(rec.risk),
                status_text,
            ]

            for col, val in enumerate(values):
                item = self._games_table.item(row, col)
                if not item:
                    item = QTableWidgetItem(val)
                    self._games_table.setItem(row, col, item)
                else:
                    item.setText(val)
                item.setData(_K_PATH, path)

                if col == 0:
                    if source == GameSource.Steam:
                        item.setForeground(QColor("#4D9DE0"))
                        item.setToolTip("Source: Steam")
                    else:
                        item.setForeground(QColor("#8EE6B1"))
                        item.setToolTip("Source: Manual")

                if col == 7:
                    if is_optimized:
                        item.setForeground(QColor("#8EE6B1"))
                    else:
                        item.setForeground(QColor("#E9EDF1"))

            self._row_analyses[row] = analysis
            self._row_recommendations[row] = rec
        finally:
            self._games_table.setUpdatesEnabled(True)

    @staticmethod
    def _extract_drive_letter(path: str) -> str:
        if len(path) >= 2 and path[1] == ":":
            return path[:2].upper()
        return "?"

    @staticmethod
    def _drive_space_info(drive_letter: str) -> str:
        root_path = drive_letter + "\\"
        try:
            usage = shutil.disk_usage(root_path)
            return f"  [{_format_bytes(usage.free)} free / {_format_bytes(usage.total)} total]"
        except OSError:
            return ""

    def _apply_stored_metadata(self):
        storage_root = self._get_storage_root()
        store = SafetyMetadataStore(storage_root)
        all_metadata = store.load_all()

        metadata_by_path: dict[str, SafetyMetadata] = {}
        for meta in all_metadata:
            lower_path = normalize_path(meta.root_path).lower()
            metadata_by_path[lower_path] = meta

        total_saved = 0
        games_optimized = 0
        total_before = 0
        total_after = 0

        for row in range(self._games_table.rowCount()):
            item = self._games_table.item(row, 0)
            if not item:
                continue
            if item.data(_K_DRIVE_HEADER):
                continue

            path_str = item.data(_K_PATH)
            if not path_str:
                continue
            normalized_path = normalize_path(path_str).lower()

            meta = metadata_by_path.get(normalized_path)
            if meta is None:
                continue

            if meta.state == SafetyOperationState.Completed:
                saved = max(meta.size_before_bytes - meta.size_after_bytes, 0)
                total_saved += saved
                games_optimized += 1
                total_before += meta.size_before_bytes
                total_after += meta.size_after_bytes

                self._update_row_status(row, f"Optimized ({_format_bytes(saved)})")

                status_item = self._games_table.item(row, 7)
                if status_item:
                    status_item.setForeground(QColor("#8EE6B1"))

                size_item = self._games_table.item(row, 2)
                if size_item:
                    size_item.setText(f"{_format_bytes(meta.size_after_bytes)} (-{_format_bytes(saved)})")

                rec_item = self._games_table.item(row, 5)
                if rec_item and meta.algorithm:
                    rec_item.setText(algorithm_to_string(meta.algorithm))

            elif meta.state == SafetyOperationState.Restored:
                self._update_row_status(row, "Restored")
            elif meta.state == SafetyOperationState.Failed:
                self._update_row_status(row, "Optimization failed")
            elif meta.state == SafetyOperationState.Planned:
                self._update_row_status(row, "Planned (not executed)")

        self._games_count_label.setText(f"\U0001f3ae {games_optimized} Optimized")
        self._space_saved_label.setText(f"\U0001f525 {_format_bytes(total_saved)} Saved")

        ratio = 0.0
        if total_before > 0:
            ratio = ((total_before - total_after) / total_before) * 100.0
        self._ratio_label.setText(f"\U0001f4ca {ratio:.1f}% Avg Ratio")

    def _update_active_row_from_metadata(self, normalized_path: str):
        storage_root = self._get_storage_root()
        store = SafetyMetadataStore(storage_root)
        metadata = store.load_by_id(SafetyMetadataStore.make_stable_id(normalized_path))
        if metadata and metadata.state == SafetyOperationState.Completed:
            self._optimize_button.setEnabled(False)
            self._restore_button.setEnabled(True)

    def _on_profile_changed(self, index: int):
        algo_map = {
            0: CompressionAlgorithm.Xpress4k,
            1: CompressionAlgorithm.Xpress8k,
            2: CompressionAlgorithm.Xpress16k,
            3: CompressionAlgorithm.Lzx,
        }
        if index not in algo_map:
            return
        self._current_algorithm = algo_map[index]

        engine = RecommendationEngine()
        for row, analysis in self._row_analyses.items():
            rec = engine.recommend_with_algorithm(analysis, self._current_algorithm)
            self._row_recommendations[row] = rec

            rec_item = self._games_table.item(row, 5)
            if rec_item:
                rec_item.setText(_recommendation_text(rec))
            risk_item = self._games_table.item(row, 6)
            if risk_item:
                risk_item.setText(risk_to_string(rec.risk))
            status_item = self._games_table.item(row, 7)
            if status_item:
                status_item.setText(_reason_text(rec))

        if self._active_analysis:
            rec = engine.recommend_with_algorithm(self._active_analysis, self._current_algorithm)
            self._active_recommendation = rec
            self._update_active_state(self._active_analysis, rec)

    def _try_get_row_analysis(self, row: int) -> tuple[GameAnalysis, CompressionRecommendation] | None:
        if row < 0 or row >= self._games_table.rowCount():
            return None
        item = self._games_table.item(row, 0)
        if not item or item.data(_K_DRIVE_HEADER):
            return None
        analysis = self._row_analyses.get(row)
        rec = self._row_recommendations.get(row)
        if analysis is None or rec is None:
            return None
        return analysis, rec

    def _update_active_state(self, analysis: GameAnalysis, recommendation: CompressionRecommendation):
        self._active_analysis = analysis
        self._active_recommendation = recommendation
        self._optimize_button.setEnabled(recommendation.action == RecommendationAction.Compress)
        has_compression = analysis.ntfs_compressed_file_count > 0 or analysis.total_bytes < analysis.logical_bytes
        self._restore_button.setEnabled(has_compression)

    def _update_row_status(self, row: int, status: str):
        item = self._games_table.item(row, 7)
        if item:
            item.setText(status)

    def _on_optimize(self):
        selected_row = self._games_table.currentRow()
        analysis = None
        recommendation = None

        if selected_row >= 0:
            result = self._try_get_row_analysis(selected_row)
            if result is None:
                QMessageBox.information(self, "Not analyzed", "Analyze this game first (click Analyze Selected).")
                return
            analysis, recommendation = result
            self._active_row = selected_row
            self._active_analysis = analysis
            self._active_recommendation = recommendation
        elif self._active_analysis and self._active_recommendation:
            analysis = self._active_analysis
            recommendation = self._active_recommendation
        else:
            QMessageBox.information(self, "No game", "Analyze a game first or select an analyzed row.")
            return

        if recommendation.action != RecommendationAction.Compress:
            QMessageBox.information(self, "Skipped", "This game is marked as Skip.")
            return

        self._set_busy(True)
        self._status_label.setText("Optimizing")
        self._progress_bar.setRange(0, int(analysis.file_count))
        self._progress_bar.setValue(0)

        if self._active_row >= 0:
            self._update_row_status(self._active_row, "Optimizing")

        def on_progress(lines_processed: int):
            QTimer.singleShot(0, lambda v=lines_processed: self._progress_bar.setValue(v))

        future = self._compression_controller.compress(analysis, recommendation, on_progress)
        self._compress_watcher = _AsyncWatcher(future, self)
        self._compress_watcher.finished.connect(self._finish_compression)

    def _finish_compression(self, result):
        self._set_busy(False)

        if isinstance(result, Exception):
            self._status_label.setText("Error")
            if self._active_row >= 0:
                self._update_row_status(self._active_row, "Error")
            QMessageBox.warning(self, "Compression failed", str(result))
            return

        cr: CompressionResult = result
        if not cr.success:
            self._status_label.setText("Error")
            if self._active_row >= 0:
                self._update_row_status(self._active_row, "Error")
            msg = cr.error_message or cr.output
            QMessageBox.warning(self, "Compression failed", msg)
            return

        saved = max(cr.bytes_before - cr.bytes_after, 0)

        if saved == 0:
            detail = "No files compressed. Folder is already NTFS-compressed or contains only incompressible data."
            self._status_label.setText(detail)
            if self._active_row >= 0:
                self._update_row_status(self._active_row, "No change")
            QMessageBox.information(self, "No savings", detail)
            self._optimize_button.setEnabled(True)
            self._restore_button.setEnabled(False)
            return

        saved_str = _format_bytes(saved)
        self._status_label.setText(f"Optimized \u2014 saved {saved_str}")

        if self._active_row >= 0:
            self._update_row_status(self._active_row, f"Optimized ({saved_str} saved)")
            size_item = self._games_table.item(self._active_row, 2)
            if size_item:
                size_item.setText(f"{_format_bytes(cr.bytes_after)} (-{saved_str})")
            status_item = self._games_table.item(self._active_row, 7)
            if status_item:
                status_item.setForeground(QColor("#8EE6B1"))

        self._apply_stored_metadata()
        self._optimize_button.setEnabled(False)
        self._restore_button.setEnabled(True)

    def _on_restore(self):
        if not self._active_analysis:
            QMessageBox.information(self, "No game", "Analyze or select an optimized game first.")
            return

        self._set_busy(True)
        self._status_label.setText("Restoring")
        self._progress_bar.setRange(0, int(self._active_analysis.file_count))
        self._progress_bar.setValue(0)

        if self._active_row >= 0:
            self._update_row_status(self._active_row, "Restoring")

        metadata = SafetyMetadata()
        metadata.id = SafetyMetadataStore.make_stable_id(self._active_analysis.root_path)
        metadata.root_path = self._active_analysis.root_path

        def on_progress(lines_processed: int):
            QTimer.singleShot(0, lambda v=lines_processed: self._progress_bar.setValue(v))

        future = self._compression_controller.restore(metadata, on_progress)
        self._restore_watcher = _AsyncWatcher(future, self)
        self._restore_watcher.finished.connect(self._finish_restore)

    def _finish_restore(self, result):
        self._set_busy(False)

        if isinstance(result, Exception):
            self._status_label.setText("Restore error")
            if self._active_row >= 0:
                self._update_row_status(self._active_row, "Restore error")
            QMessageBox.warning(self, "Restore failed", str(result))
            return

        cr: CompressionResult = result
        if not cr.success:
            self._status_label.setText("Restore error")
            if self._active_row >= 0:
                self._update_row_status(self._active_row, "Restore error")
            msg = cr.error_message or cr.output
            QMessageBox.warning(self, "Restore failed", msg)
            return

        self._status_label.setText("Restored")
        if self._active_row >= 0:
            self._update_row_status(self._active_row, "Restored")
            size_item = self._games_table.item(self._active_row, 2)
            if size_item:
                size_item.setText(_format_bytes(cr.bytes_after))
            status_item = self._games_table.item(self._active_row, 7)
            if status_item:
                status_item.setForeground(QColor("#E9EDF1"))

        self._apply_stored_metadata()
        self._optimize_button.setEnabled(True)
        self._restore_button.setEnabled(False)

    def _on_remove_game(self):
        current_row = self._games_table.currentRow()
        if current_row < 0 or current_row >= self._games_table.rowCount():
            QMessageBox.information(self, "No selection", "Select a game row to remove.")
            return

        item = self._games_table.item(current_row, 0)
        if not item:
            return
        if item.data(_K_DRIVE_HEADER):
            return

        path = item.data(_K_PATH)
        if not path:
            return

        if Path(path).is_dir():
            normalized_path = normalize_path(path)
            storage_root = self._get_storage_root()
            store = SafetyMetadataStore(storage_root)
            metadata = store.load_by_id(SafetyMetadataStore.make_stable_id(normalized_path))
            if metadata and metadata.state == SafetyOperationState.Completed:
                QMessageBox.warning(
                    self, "Cannot remove",
                    "This game is currently optimized. You must Restore it before removing it from the library."
                )
                return

        self._library_games = [
            g for g in self._library_games
            if g.install_path.lower() != path.lower()
        ]
        self._save_library()
        self._refresh_table_view()
        self._apply_stored_metadata()

    def _set_busy(self, busy: bool):
        self._select_folder_button.setEnabled(not busy)
        self._scan_steam_button.setEnabled(not busy)
        self._analyze_selected_button.setEnabled(not busy)
        self._analyze_all_button.setEnabled(not busy)
        self._settings_button.setEnabled(not busy)
        if busy:
            self._optimize_button.setEnabled(False)
            self._restore_button.setEnabled(False)
        self._cancel_button.setEnabled(busy)
        self._remove_button.setEnabled(not busy)
        self._profile_combo.setEnabled(not busy)
        if busy:
            self._progress_bar.setRange(0, 0)
            self._progress_bar.setValue(0)
        else:
            self._progress_bar.setRange(0, 1)
            self._progress_bar.setValue(1)

    def _on_settings(self):
        dialog = SettingsDialog(self)
        if dialog.exec() == dialog.DialogCode.Accepted:
            index = dialog.default_algorithm_index()
            self._profile_combo.setCurrentIndex(index)
            self._on_profile_changed(index)
