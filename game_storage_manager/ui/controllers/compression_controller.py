from __future__ import annotations

import os
import threading
from concurrent.futures import Future
from typing import Callable

from game_storage_manager.core.analyzer.game_analysis import GameAnalysis
from game_storage_manager.core.compressor.compression_task import CompressionResult, Compressor
from game_storage_manager.core.rules_engine.recommendation_engine import CompressionRecommendation
from game_storage_manager.core.safety.safety_metadata import SafetyMetadata
from game_storage_manager.core.safety.safety_metadata_store import SafetyMetadataStore
from game_storage_manager.system.filesystem.filesystem import join_path


def _app_storage_root() -> str:
    local_app_data = os.environ.get("LOCALAPPDATA")
    if local_app_data:
        return join_path(local_app_data, "GameStorageManager/metadata")
    return "metadata"


class CompressionController:
    def __init__(self):
        self._cancel_flag = threading.Event()

    def compress(
        self,
        analysis: GameAnalysis,
        recommendation: CompressionRecommendation,
        on_progress: Callable[[int], None] | None = None,
    ) -> Future[CompressionResult]:
        self._cancel_flag.clear()
        flag = self._cancel_flag
        future: Future[CompressionResult] = Future()

        def _run():
            try:
                compressor = Compressor()
                store = SafetyMetadataStore(_app_storage_root())
                result = compressor.compress(analysis, recommendation, store, on_progress, flag)
                future.set_result(result)
            except Exception as e:
                future.set_exception(e)

        thread = threading.Thread(target=_run, daemon=True)
        thread.start()
        return future

    def restore(
        self,
        metadata: SafetyMetadata,
        on_progress: Callable[[int], None] | None = None,
    ) -> Future[CompressionResult]:
        target_path = metadata.root_path
        self._cancel_flag.clear()
        flag = self._cancel_flag
        future: Future[CompressionResult] = Future()

        def _run():
            try:
                compressor = Compressor()
                store = SafetyMetadataStore(_app_storage_root())
                result = compressor.restore(metadata, store, target_path, on_progress, flag)
                future.set_result(result)
            except Exception as e:
                future.set_exception(e)

        thread = threading.Thread(target=_run, daemon=True)
        thread.start()
        return future

    def cancel(self):
        self._cancel_flag.set()
