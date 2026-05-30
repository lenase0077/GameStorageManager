from __future__ import annotations

import sys

from game_storage_manager.core.analyzer.game_analyzer import GameAnalyzer
from game_storage_manager.core.rules_engine.recommendation_engine import (
    CompressionAlgorithm,
    OptimizationProfile,
    RecommendationEngine,
    action_to_string,
    algorithm_to_string,
    parse_compression_algorithm,
    risk_to_string,
)
from game_storage_manager.core.scanner.steam_scanner import SteamScanner
from game_storage_manager.system.process.compact_process_adapter import (
    CompactProcessAdapter,
    to_display_string,
)


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


def _print_usage():
    print("Game Storage Manager CLI")
    print()
    print("Usage:")
    print("  gsm_cli analyze <folder>")
    print("  gsm_cli scan-steam")
    print("  gsm_cli compact-command <compress|restore> <folder> [xpress4k|xpress8k|xpress16k|lzx]")


def _print_recommendation(rec):
    print(f"Recommendation: {action_to_string(rec.action)}")
    if rec.algorithm is not None:
        print(f"Algorithm: {algorithm_to_string(rec.algorithm)}")
    print(f"Risk: {risk_to_string(rec.risk)}")
    if rec.reasons:
        print("Reasons:")
        for reason in rec.reasons:
            print(f"  - {reason}")


def _analyze_folder(folder: str) -> int:
    analyzer = GameAnalyzer()
    analysis = analyzer.analyze(folder)

    if not analysis.is_valid:
        print(f"Invalid folder: {analysis.error_message}", file=sys.stderr)
        return 2

    print(f"Path: {analysis.root_path}")
    print(f"Total size: {_format_bytes(analysis.total_bytes)}")
    print(f"Files: {analysis.file_count}")
    print(f"Directories: {analysis.directory_count}")
    print(f"Already-compressed assets: {analysis.already_compressed_file_count}")
    print(f"Largest file: {_format_bytes(analysis.largest_file_bytes)}")

    if analysis.inaccessible_entry_count > 0:
        print(f"Inaccessible entries: {analysis.inaccessible_entry_count}")

    if analysis.extensions:
        print("Top extensions:")
        for ext in analysis.extensions[:8]:
            print(f"  {ext.extension}: {ext.file_count} files, {_format_bytes(ext.total_bytes)}")

    engine = RecommendationEngine()
    rec = engine.recommend(analysis, OptimizationProfile.Balanced)
    print()
    _print_recommendation(rec)
    return 0


def _print_compact_command(args: list[str]) -> int:
    if len(args) < 2:
        _print_usage()
        return 1

    operation = args[0]
    folder = args[1]
    adapter = CompactProcessAdapter()

    if operation == "restore":
        cmd = adapter.build_restore_command(folder)
        print(to_display_string(cmd))
        return 0

    if operation != "compress":
        print(f"Unknown operation: {operation}", file=sys.stderr)
        return 1

    algorithm = CompressionAlgorithm.Xpress8k
    if len(args) >= 3:
        parsed = parse_compression_algorithm(args[2])
        if parsed is None:
            print(f"Unknown algorithm: {args[2]}", file=sys.stderr)
            return 1
        algorithm = parsed

    cmd = adapter.build_compress_command(folder, algorithm)
    print(to_display_string(cmd))
    return 0


def _scan_steam() -> int:
    scanner = SteamScanner()
    games = scanner.scan_installed_games()
    print(f"Steam games found: {len(games)}")
    for game in games:
        print(f"- [{game.source_id}] {game.name}")
        print(f"  Library: {game.library_path}")
        print(f"  Path: {game.install_path}")
    return 0


def main():
    args = sys.argv[1:]

    if not args:
        _print_usage()
        sys.exit(1)

    command = args[0]

    if command == "analyze":
        if len(args) < 2:
            _print_usage()
            sys.exit(1)
        sys.exit(_analyze_folder(args[1]))

    if command == "compact-command":
        sys.exit(_print_compact_command(args[1:]))

    if command == "scan-steam":
        sys.exit(_scan_steam())

    _print_usage()
    sys.exit(1)


if __name__ == "__main__":
    main()
