from __future__ import annotations

import threading
from dataclasses import dataclass
from datetime import datetime, timezone
from typing import Callable

from game_storage_manager.core.analyzer.game_analysis import GameAnalysis
from game_storage_manager.core.rules_engine.recommendation_engine import (
    CompressionRecommendation,
    RecommendationAction,
)
from game_storage_manager.core.safety.safety_metadata import SafetyMetadata, SafetyOperationState
from game_storage_manager.core.safety.safety_metadata_store import SafetyMetadataStore
from game_storage_manager.system.filesystem.filesystem import directory_exists, directory_size
from game_storage_manager.system.process.compact_process_adapter import (
    CompactOutputMetrics,
    CompactProcessAdapter,
)


@dataclass
class CompressionResult:
    success: bool = False
    error_message: str = ""
    bytes_before: int = 0
    bytes_after: int = 0
    exit_code: int = -1
    output: str = ""
    metrics: CompactOutputMetrics | None = None


def _utc_now() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


class Compressor:
    def __init__(self):
        self._adapter = CompactProcessAdapter()

    def compress(
        self,
        analysis: GameAnalysis,
        recommendation: CompressionRecommendation,
        metadata_store: SafetyMetadataStore,
        on_progress: Callable[[int], None] | None = None,
        cancel_flag: threading.Event | None = None,
    ) -> CompressionResult:
        result = CompressionResult()
        result.bytes_before = analysis.total_bytes

        if not analysis.is_valid:
            result.error_message = "Cannot compress: analysis is invalid."
            return result

        if recommendation.action != RecommendationAction.Compress:
            result.error_message = "Cannot compress: game is marked as Skip."
            return result

        if recommendation.algorithm is None:
            result.error_message = "Cannot compress: no algorithm selected."
            return result

        if not directory_exists(analysis.root_path):
            result.error_message = "Target path no longer exists."
            return result

        metadata = metadata_store.create_planned_metadata(
            analysis, recommendation, analysis.game_name, "manual"
        )
        metadata.state = SafetyOperationState.Running
        metadata.created_at_utc = _utc_now()
        metadata.updated_at_utc = metadata.created_at_utc

        if not metadata_store.save(metadata):
            result.error_message = "Failed to save safety metadata before compression."
            return result

        line_count = 0

        def output_callback(chunk: str) -> None:
            nonlocal line_count
            if not on_progress:
                return
            line_count += chunk.count("\n")
            on_progress(line_count)

        command = self._adapter.build_compress_command(analysis.root_path, recommendation.algorithm)
        process_result = self._adapter.run(command, output_callback, cancel_flag)

        result.exit_code = process_result.exit_code
        result.output = process_result.output
        result.metrics = CompactProcessAdapter.parse_compress_output(process_result.output)

        if process_result.exit_code != 0:
            metadata.state = SafetyOperationState.Failed
            metadata.notes.append(f"compact.exe exit code: {process_result.exit_code}")
            metadata.updated_at_utc = _utc_now()
            metadata_store.save(metadata)
            result.error_message = "compact.exe returned non-zero exit code."
            return result

        if result.metrics and result.metrics.parsed and analysis.total_bytes > 0:
            unchanged_bytes = analysis.total_bytes - result.metrics.bytes_before_from_output
            metadata.size_after_bytes = unchanged_bytes + result.metrics.bytes_after_from_output
        else:
            metadata.size_after_bytes = directory_size(analysis.root_path)

        metadata.state = SafetyOperationState.Completed
        metadata.updated_at_utc = _utc_now()

        if not metadata_store.save(metadata):
            result.error_message = "Compression succeeded but failed to save metadata."
            result.success = True
            result.bytes_after = metadata.size_after_bytes
            return result

        result.success = True
        result.bytes_after = metadata.size_after_bytes
        return result

    def restore(
        self,
        metadata: SafetyMetadata,
        metadata_store: SafetyMetadataStore,
        target_path: str,
        on_progress: Callable[[int], None] | None = None,
        cancel_flag: threading.Event | None = None,
    ) -> CompressionResult:
        result = CompressionResult()
        result.bytes_before = metadata.size_after_bytes

        if not directory_exists(target_path):
            result.error_message = "Target path no longer exists."
            return result

        line_count = 0

        def output_callback(chunk: str) -> None:
            nonlocal line_count
            if not on_progress:
                return
            line_count += chunk.count("\n")
            on_progress(line_count)

        command = self._adapter.build_restore_command(target_path)
        process_result = self._adapter.run(command, output_callback, cancel_flag)

        result.exit_code = process_result.exit_code
        result.output = process_result.output

        if process_result.exit_code != 0:
            updated = SafetyMetadata(
                schema_version=metadata.schema_version,
                id=metadata.id,
                game_name=metadata.game_name,
                root_path=metadata.root_path,
                source=metadata.source,
                size_before_bytes=metadata.size_before_bytes,
                size_after_bytes=metadata.size_after_bytes,
                file_count_before=metadata.file_count_before,
                algorithm=metadata.algorithm,
                state=SafetyOperationState.Failed,
                created_at_utc=metadata.created_at_utc,
                updated_at_utc=metadata.updated_at_utc,
                notes=list(metadata.notes),
            )
            updated.notes.append(f"Restore failed. compact.exe exit code: {process_result.exit_code}")
            metadata_store.save(updated)
            result.error_message = "compact.exe returned non-zero exit code during restore."
            return result

        result.bytes_after = directory_size(target_path)
        result.success = True

        updated = SafetyMetadata(
            schema_version=metadata.schema_version,
            id=metadata.id,
            game_name=metadata.game_name,
            root_path=metadata.root_path,
            source=metadata.source,
            size_before_bytes=metadata.size_before_bytes,
            size_after_bytes=result.bytes_after,
            file_count_before=metadata.file_count_before,
            algorithm=metadata.algorithm,
            state=SafetyOperationState.Restored,
            created_at_utc=metadata.created_at_utc,
            updated_at_utc=metadata.updated_at_utc,
            notes=list(metadata.notes),
        )
        updated.notes.append("Restored to original state.")
        metadata_store.save(updated)

        return result
