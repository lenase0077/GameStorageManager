from __future__ import annotations

import glob
from datetime import datetime, timezone
from pathlib import Path

from game_storage_manager.core.analyzer.game_analysis import GameAnalysis
from game_storage_manager.core.rules_engine.recommendation_engine import (
    CompressionRecommendation,
    algorithm_to_string,
    parse_compression_algorithm,
)
from game_storage_manager.core.safety.safety_metadata import (
    SafetyMetadata,
    SafetyOperationState,
    parse_safety_operation_state,
    state_to_string,
)
from game_storage_manager.system.filesystem.filesystem import (
    ensure_directory_exists,
    join_path,
    normalize_path,
)


def _now_utc_iso8601() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _escape_value(value: str) -> str:
    result = []
    for ch in value:
        if ch == "\\":
            result.append("\\\\")
        elif ch == "\n":
            result.append("\\n")
        elif ch == "\r":
            result.append("\\r")
        elif ch == "|":
            result.append("\\p")
        else:
            result.append(ch)
    return "".join(result)


def _unescape_value(value: str) -> str:
    result = []
    i = 0
    while i < len(value):
        ch = value[i]
        if ch != "\\" or i + 1 >= len(value):
            result.append(ch)
            i += 1
            continue
        i += 1
        nxt = value[i]
        if nxt == "n":
            result.append("\n")
        elif nxt == "r":
            result.append("\r")
        elif nxt == "p":
            result.append("|")
        elif nxt == "\\":
            result.append("\\")
        else:
            result.append(nxt)
        i += 1
    return "".join(result)


def _join_escaped_notes(notes: list[str]) -> str:
    return "|".join(_escape_value(n) for n in notes)


def _parse_notes(notes_text: str) -> list[str]:
    if not notes_text:
        return []
    return [_unescape_value(item) for item in notes_text.split("|")]


def _fallback_name_from_path(path: str) -> str:
    normalized = normalize_path(path)
    normalized = normalized.replace("/", "\\")
    sep = normalized.rfind("\\")
    if sep == -1:
        return normalized
    return normalized[sep + 1:]


def _to_lower(value: str) -> str:
    return value.lower()


class SafetyMetadataStore:
    def __init__(self, storage_root: str):
        self._storage_root = normalize_path(storage_root)

    @property
    def storage_root(self) -> str:
        return self._storage_root

    def metadata_path_for_id(self, id: str) -> str:
        return join_path(self._storage_root, id + ".gsmmeta")

    def create_planned_metadata(
        self,
        analysis: GameAnalysis,
        recommendation: CompressionRecommendation,
        game_name: str,
        source: str,
    ) -> SafetyMetadata:
        metadata = SafetyMetadata()
        metadata.id = self.make_stable_id(analysis.root_path)
        metadata.game_name = game_name if game_name else _fallback_name_from_path(analysis.root_path)
        metadata.root_path = normalize_path(analysis.root_path)
        metadata.source = source if source else "manual"
        metadata.size_before_bytes = analysis.total_bytes
        metadata.file_count_before = analysis.file_count
        metadata.algorithm = recommendation.algorithm
        metadata.state = SafetyOperationState.Planned
        metadata.created_at_utc = _now_utc_iso8601()
        metadata.updated_at_utc = metadata.created_at_utc
        for reason in recommendation.reasons:
            metadata.notes.append("recommendation:" + reason)
        return metadata

    def save(self, metadata: SafetyMetadata) -> bool:
        if not metadata.id or not ensure_directory_exists(self._storage_root):
            return False
        try:
            path = self.metadata_path_for_id(metadata.id)
            with open(path, "w", encoding="utf-8", newline="") as f:
                f.write(serialize_safety_metadata(metadata))
            return True
        except OSError:
            return False

    def load_by_id(self, id: str) -> SafetyMetadata | None:
        try:
            path = self.metadata_path_for_id(id)
            with open(path, "r", encoding="utf-8") as f:
                return deserialize_safety_metadata(f.read())
        except OSError:
            return None

    def load_all(self) -> list[SafetyMetadata]:
        records: list[SafetyMetadata] = []
        search_pattern = join_path(self._storage_root, "*.gsmmeta")
        for file_path in glob.glob(search_pattern):
            file_name = Path(file_path).stem
            metadata = self.load_by_id(file_name)
            if metadata is not None:
                records.append(metadata)
        records.sort(key=lambda m: m.updated_at_utc, reverse=True)
        return records

    @staticmethod
    def make_stable_id(root_path: str) -> str:
        normalized = _to_lower(normalize_path(root_path))
        h = 1469598103934665603
        for ch in normalized:
            h ^= ord(ch)
            h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
        return format(h, "x")


def serialize_safety_metadata(metadata: SafetyMetadata) -> str:
    lines = [
        f"schemaVersion={metadata.schema_version}",
        f"id={_escape_value(metadata.id)}",
        f"gameName={_escape_value(metadata.game_name)}",
        f"rootPath={_escape_value(metadata.root_path)}",
        f"source={_escape_value(metadata.source)}",
        f"sizeBeforeBytes={metadata.size_before_bytes}",
        f"sizeAfterBytes={metadata.size_after_bytes}",
        f"fileCountBefore={metadata.file_count_before}",
        f"algorithm={algorithm_to_string(metadata.algorithm) if metadata.algorithm else ''}",
        f"state={state_to_string(metadata.state)}",
        f"createdAtUtc={_escape_value(metadata.created_at_utc)}",
        f"updatedAtUtc={_escape_value(metadata.updated_at_utc)}",
        f"notes={_join_escaped_notes(metadata.notes)}",
    ]
    return "\n".join(lines) + "\n"


def deserialize_safety_metadata(text: str) -> SafetyMetadata | None:
    values: dict[str, str] = {}
    for line in text.splitlines():
        if not line:
            continue
        sep = line.find("=")
        if sep == -1:
            return None
        values[line[:sep]] = line[sep + 1:]

    if values.get("schemaVersion") != "1":
        return None

    metadata = SafetyMetadata()
    metadata.schema_version = 1
    metadata.id = _unescape_value(values.get("id", ""))
    metadata.game_name = _unescape_value(values.get("gameName", ""))
    metadata.root_path = _unescape_value(values.get("rootPath", ""))
    metadata.source = _unescape_value(values.get("source", ""))

    try:
        metadata.size_before_bytes = int(values.get("sizeBeforeBytes", "0"))
        metadata.size_after_bytes = int(values.get("sizeAfterBytes", "0"))
        metadata.file_count_before = int(values.get("fileCountBefore", "0"))
    except ValueError:
        return None

    state = parse_safety_operation_state(values.get("state", ""))
    if state is None:
        return None
    metadata.state = state

    metadata.created_at_utc = _unescape_value(values.get("createdAtUtc", ""))
    metadata.updated_at_utc = _unescape_value(values.get("updatedAtUtc", ""))
    metadata.notes = _parse_notes(values.get("notes", ""))

    algo_str = values.get("algorithm", "")
    if algo_str:
        metadata.algorithm = parse_compression_algorithm(algo_str)
        if metadata.algorithm is None:
            return None

    return metadata
