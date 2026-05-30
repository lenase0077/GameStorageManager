from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum

from game_storage_manager.core.rules_engine.recommendation_engine import CompressionAlgorithm


class SafetyOperationState(Enum):
    Planned = "Planned"
    Running = "Running"
    Completed = "Completed"
    Failed = "Failed"
    Restored = "Restored"


def state_to_string(state: SafetyOperationState) -> str:
    return state.value


def parse_safety_operation_state(value: str) -> SafetyOperationState | None:
    lower = value.lower()
    for state in SafetyOperationState:
        if state.value.lower() == lower:
            return state
    return None


@dataclass
class SafetyMetadata:
    schema_version: int = 1
    id: str = ""
    game_name: str = ""
    root_path: str = ""
    source: str = ""
    size_before_bytes: int = 0
    size_after_bytes: int = 0
    file_count_before: int = 0
    algorithm: CompressionAlgorithm | None = None
    state: SafetyOperationState = SafetyOperationState.Planned
    created_at_utc: str = ""
    updated_at_utc: str = ""
    notes: list[str] = field(default_factory=list)
