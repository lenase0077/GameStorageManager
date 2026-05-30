from __future__ import annotations

import threading
from concurrent.futures import Future

from game_storage_manager.core.analyzer.game_analysis import GameAnalysis
from game_storage_manager.core.analyzer.game_analyzer import GameAnalyzer
from game_storage_manager.core.scanner.game_entry import GameEntry
from game_storage_manager.core.scanner.steam_scanner import SteamScanner


class AnalysisController:
    def __init__(self):
        self._cancel_flag = threading.Event()

    def analyze_folder(self, folder_path: str, game_name: str = "") -> Future[GameAnalysis]:
        self._cancel_flag.clear()
        flag = self._cancel_flag

        future: Future[GameAnalysis] = Future()

        def _run():
            try:
                analyzer = GameAnalyzer()
                result = analyzer.analyze(folder_path, game_name, flag)
                future.set_result(result)
            except Exception as e:
                future.set_exception(e)

        thread = threading.Thread(target=_run, daemon=True)
        thread.start()
        return future

    def scan_steam_games(self) -> Future[list[GameEntry]]:
        future: Future[list[GameEntry]] = Future()

        def _run():
            try:
                scanner = SteamScanner()
                result = scanner.scan_installed_games()
                future.set_result(result)
            except Exception as e:
                future.set_exception(e)

        thread = threading.Thread(target=_run, daemon=True)
        thread.start()
        return future

    def cancel(self):
        self._cancel_flag.set()
