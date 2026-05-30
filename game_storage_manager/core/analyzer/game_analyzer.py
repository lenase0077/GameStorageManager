from __future__ import annotations

import threading
from pathlib import Path

from game_storage_manager.core.analyzer.game_analysis import ExtensionStats, GameAnalysis
from game_storage_manager.system.filesystem.filesystem import (
    directory_exists,
    file_extension,
    get_compressed_file_size,
    is_ntfs_compressed,
    normalize_path,
)

_COMPRESSED_EXTENSIONS = frozenset({
    ".7z", ".arc", ".bik", ".bnk", ".cab", ".dds", ".gz", ".jpg", ".jpeg", ".mp3",
    ".mp4", ".ogg", ".png", ".rar", ".usm", ".wem", ".zip", ".zst",
})

_ANTI_CHEAT_NAMES = [
    "battleye", "belauncher", "beservice", "battleye.sys",
    "easyanticheat", "easyanticheat.sys",
    "vgk", "vgk.sys", "vanguard",
    "faceit", "faceit.sys",
    "equ8", "equ8.dll",
    "xigncode", "x3.xem", "xmag.xem",
    "punkbuster", "pbsvc", "pbsetup", "pbcl",
    "wellbia", "npgg", "uncheater",
    "ricochet",
    "arbiter.sys", "arbiter",
    "tencentprotect",
    "sguard",
    "mrac", "mrac.sys",
    "gameguard",
    "hackshield",
    "nexonguard",
    "blackcipher",
]


def _is_anti_cheat_file_name(file_name: str) -> bool:
    lower = file_name.lower()
    return any(ac in lower for ac in _ANTI_CHEAT_NAMES)


def _normalize_extension(file_name: str) -> str:
    ext = file_extension(file_name)
    if not ext:
        return "<none>"
    return ext.lower()


class GameAnalyzer:
    def analyze(
        self,
        root_path: str,
        game_name: str = "",
        cancel_flag: threading.Event | None = None,
    ) -> GameAnalysis:
        analysis = GameAnalysis()
        analysis.root_path = normalize_path(root_path)
        analysis.game_name = game_name

        if not directory_exists(analysis.root_path):
            analysis.error_message = "Path does not exist."
            return analysis

        analysis.is_valid = True
        ext_stats: dict[str, ExtensionStats] = {}

        try:
            for entry in Path(analysis.root_path).rglob("*"):
                if cancel_flag and cancel_flag.is_set():
                    analysis.is_valid = False
                    analysis.error_message = "Analysis cancelled by user."
                    return analysis

                file_name = entry.name

                if entry.is_dir():
                    analysis.directory_count += 1
                    if _is_anti_cheat_file_name(file_name):
                        analysis.contains_anti_cheat_files = True
                    continue

                if entry.is_file():
                    try:
                        log_size = entry.stat().st_size
                    except OSError:
                        analysis.inaccessible_entry_count += 1
                        continue

                    phys_size = get_compressed_file_size(str(entry))
                    if phys_size is None:
                        phys_size = log_size

                    analysis.file_count += 1
                    analysis.total_bytes += phys_size
                    analysis.logical_bytes += log_size
                    analysis.largest_file_bytes = max(analysis.largest_file_bytes, phys_size)

                    if is_ntfs_compressed(str(entry)):
                        analysis.ntfs_compressed_file_count += 1
                        analysis.ntfs_compressed_bytes += phys_size

                    ext = _normalize_extension(file_name)
                    if ext not in ext_stats:
                        ext_stats[ext] = ExtensionStats(extension=ext)
                    ext_stats[ext].file_count += 1
                    ext_stats[ext].total_bytes += phys_size

                    if self.is_known_compressed_extension(ext):
                        analysis.already_compressed_file_count += 1
                        analysis.already_compressed_bytes += phys_size

                    if not analysis.contains_anti_cheat_files and _is_anti_cheat_file_name(file_name):
                        analysis.contains_anti_cheat_files = True

        except PermissionError:
            analysis.inaccessible_entry_count += 1

        analysis.extensions = sorted(ext_stats.values(), key=lambda s: s.total_bytes, reverse=True)
        return analysis

    @staticmethod
    def is_known_compressed_extension(extension: str) -> bool:
        return extension in _COMPRESSED_EXTENSIONS
