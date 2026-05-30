from __future__ import annotations

from dataclasses import dataclass
from enum import Enum


class GameSource(Enum):
    Manual = "manual"
    Steam = "steam"
    Epic = "epic"


def game_source_to_string(source: GameSource) -> str:
    return source.value


def parse_game_source(value: str) -> GameSource:
    lower = value.lower()
    for source in GameSource:
        if source.value == lower:
            return source
    return GameSource.Manual


@dataclass
class GameEntry:
    source: GameSource = GameSource.Manual
    source_id: str = ""
    name: str = ""
    install_path: str = ""
    library_path: str = ""
    size_bytes: int = 0
    installed: bool = True
