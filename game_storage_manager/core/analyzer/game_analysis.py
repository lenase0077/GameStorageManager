from __future__ import annotations

from dataclasses import dataclass, field


@dataclass
class ExtensionStats:
    extension: str = ""
    file_count: int = 0
    total_bytes: int = 0


@dataclass
class GameAnalysis:
    is_valid: bool = False
    root_path: str = ""
    game_name: str = ""
    error_message: str = ""

    total_bytes: int = 0
    logical_bytes: int = 0
    file_count: int = 0
    directory_count: int = 0
    inaccessible_entry_count: int = 0

    already_compressed_file_count: int = 0
    already_compressed_bytes: int = 0
    largest_file_bytes: int = 0

    ntfs_compressed_file_count: int = 0
    ntfs_compressed_bytes: int = 0

    contains_anti_cheat_files: bool = False

    extensions: list[ExtensionStats] = field(default_factory=list)

    def already_compressed_byte_ratio(self) -> float:
        if self.total_bytes == 0:
            return 0.0
        return self.already_compressed_bytes / self.total_bytes

    def ntfs_compressed_byte_ratio(self) -> float:
        if self.total_bytes == 0:
            return 0.0
        return self.ntfs_compressed_bytes / self.total_bytes
