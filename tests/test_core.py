from __future__ import annotations

import os
import shutil
import tempfile

from game_storage_manager.core.analyzer.game_analysis import GameAnalysis
from game_storage_manager.core.rules_engine.recommendation_engine import (
    CompressionAlgorithm,
    OptimizationProfile,
    RecommendationAction,
    RecommendationEngine,
    RecommendationRisk,
)
from game_storage_manager.core.safety.safety_metadata import SafetyMetadata, SafetyOperationState
from game_storage_manager.core.safety.safety_metadata_store import (
    SafetyMetadataStore,
    deserialize_safety_metadata,
    serialize_safety_metadata,
)
from game_storage_manager.core.scanner.game_entry import GameSource
from game_storage_manager.core.scanner.steam_scanner import SteamScanner
from game_storage_manager.system.process.compact_process_adapter import (
    CompactProcessAdapter,
    to_display_string,
)

GIBIBYTE = 1024 * 1024 * 1024


def _make_analysis(total_bytes: int, compressed_bytes: int) -> GameAnalysis:
    analysis = GameAnalysis()
    analysis.is_valid = True
    analysis.root_path = "C:\\Games\\Example"
    analysis.total_bytes = total_bytes
    analysis.file_count = 100
    analysis.already_compressed_bytes = compressed_bytes
    analysis.already_compressed_file_count = 80 if compressed_bytes > 0 else 0
    return analysis


class TestRecommendationEngine:
    def test_balanced_large_game_uses_xpress8k(self):
        analysis = _make_analysis(60 * GIBIBYTE, 10 * GIBIBYTE)
        engine = RecommendationEngine()
        rec = engine.recommend(analysis, OptimizationProfile.Balanced)
        assert rec.action == RecommendationAction.Compress
        assert rec.algorithm == CompressionAlgorithm.Xpress8k

    def test_mostly_compressed_assets_are_skipped(self):
        analysis = _make_analysis(10 * GIBIBYTE, 9 * GIBIBYTE)
        engine = RecommendationEngine()
        rec = engine.recommend(analysis, OptimizationProfile.Balanced)
        assert rec.action == RecommendationAction.Skip
        assert rec.algorithm is None
        assert rec.risk == RecommendationRisk.Medium


class TestCompactProcessAdapter:
    def test_compress_command_quotes_paths(self):
        adapter = CompactProcessAdapter()
        cmd = adapter.build_compress_command("C:\\Games\\Example Game", CompressionAlgorithm.Lzx)
        display = to_display_string(cmd)
        assert display == 'compact.exe /c /s /a /i /exe:LZX "C:\\Games\\Example Game\\*"'

    def test_restore_command_uses_uncompress(self):
        adapter = CompactProcessAdapter()
        cmd = adapter.build_restore_command("C:\\Games\\Example Game")
        display = to_display_string(cmd)
        assert display == 'compact.exe /u /s "C:\\Games\\Example Game\\*"'


class TestSafetyMetadata:
    def test_round_trip(self):
        metadata = SafetyMetadata()
        metadata.id = "sample-id"
        metadata.game_name = "Example | Game"
        metadata.root_path = "D:\\SteamLibrary\\steamapps\\common\\Example Game"
        metadata.source = "steam"
        metadata.size_before_bytes = 123456
        metadata.size_after_bytes = 100000
        metadata.file_count_before = 42
        metadata.algorithm = CompressionAlgorithm.Xpress8k
        metadata.state = SafetyOperationState.Planned
        metadata.created_at_utc = "2026-05-06T00:00:00Z"
        metadata.updated_at_utc = "2026-05-06T00:01:00Z"
        metadata.notes = ["recommendation:balanced-default", "path can contain | and \\ characters"]

        text = serialize_safety_metadata(metadata)
        parsed = deserialize_safety_metadata(text)

        assert parsed is not None
        assert parsed.id == metadata.id
        assert parsed.game_name == metadata.game_name
        assert parsed.root_path == metadata.root_path
        assert parsed.source == metadata.source
        assert parsed.size_before_bytes == metadata.size_before_bytes
        assert parsed.size_after_bytes == metadata.size_after_bytes
        assert parsed.file_count_before == metadata.file_count_before
        assert parsed.algorithm == CompressionAlgorithm.Xpress8k
        assert parsed.state == SafetyOperationState.Planned
        assert parsed.notes == metadata.notes

    def test_store_saves_and_loads(self):
        tmpdir = tempfile.mkdtemp()
        try:
            analysis = _make_analysis(12 * GIBIBYTE, 2 * GIBIBYTE)
            engine = RecommendationEngine()
            rec = engine.recommend(analysis, OptimizationProfile.Balanced)

            store = SafetyMetadataStore(tmpdir)
            metadata = store.create_planned_metadata(analysis, rec, "Example Game", "manual")

            assert metadata.id
            assert metadata.root_path == "C:\\Games\\Example"
            assert metadata.game_name == "Example Game"
            assert metadata.source == "manual"
            assert metadata.size_before_bytes == analysis.total_bytes
            assert metadata.algorithm is not None

            assert store.save(metadata)

            loaded = store.load_by_id(metadata.id)
            assert loaded is not None
            assert loaded.id == metadata.id
            assert loaded.root_path == metadata.root_path
            assert loaded.algorithm == metadata.algorithm
        finally:
            shutil.rmtree(tmpdir, ignore_errors=True)

    def test_stable_ids_normalize_drive_paths(self):
        first = SafetyMetadataStore.make_stable_id("D:/SteamLibrary/steamapps/common/Game")
        second = SafetyMetadataStore.make_stable_id("d:\\SteamLibrary\\steamapps\\common\\Game\\")
        assert first == second


class TestSteamScanner:
    def test_library_folders_multiple_drives(self):
        vdf = '''
        "libraryfolders"
        {
            "0"
            {
                "path" "C:\\\\Program Files (x86)\\\\Steam"
            }
            "1"
            {
                "path" "D:\\\\SteamLibrary"
            }
            "2" "E:\\\\Archive\\\\SteamLibrary"
            "3" "d:\\\\SteamLibrary\\\\"
        }
        '''
        libraries = SteamScanner.parse_library_folders_vdf("C:\\Program Files (x86)\\Steam", vdf)
        assert len(libraries) == 3
        assert libraries[0] == "C:\\Program Files (x86)\\Steam"
        assert libraries[1] == "D:\\SteamLibrary"
        assert libraries[2] == "E:\\Archive\\SteamLibrary"

    def test_app_manifest_builds_install_path(self):
        acf = '''
        "AppState"
        {
            "appid" "1245620"
            "name" "ELDEN RING"
            "installdir" "ELDEN RING"
        }
        '''
        game = SteamScanner.parse_app_manifest("D:\\SteamLibrary", acf)
        assert game is not None
        assert game.source == GameSource.Steam
        assert game.source_id == "1245620"
        assert game.name == "ELDEN RING"
        assert game.library_path == "D:\\SteamLibrary"
        assert game.install_path == "D:\\SteamLibrary\\steamapps\\common\\ELDEN RING"

    def test_scanner_reads_fixture_library(self):
        tmpdir = tempfile.mkdtemp()
        try:
            steam_apps = os.path.join(tmpdir, "steamapps")
            os.makedirs(steam_apps, exist_ok=True)

            manifest_path = os.path.join(steam_apps, "appmanifest_480.acf")
            with open(manifest_path, "w") as f:
                f.write('"AppState"\n')
                f.write('{\n')
                f.write('    "appid" "480"\n')
                f.write('    "name" "Spacewar"\n')
                f.write('    "installdir" "Spacewar"\n')
                f.write('}\n')

            scanner = SteamScanner()
            games = scanner.scan_library(tmpdir)
            assert len(games) == 1
            assert games[0].source_id == "480"
            assert games[0].name == "Spacewar"
            assert "steamapps" in games[0].install_path
            assert "common" in games[0].install_path
            assert "Spacewar" in games[0].install_path
        finally:
            shutil.rmtree(tmpdir, ignore_errors=True)
