from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum


class CompressionAlgorithm(Enum):
    Xpress4k = "XPRESS4K"
    Xpress8k = "XPRESS8K"
    Xpress16k = "XPRESS16K"
    Lzx = "LZX"


class OptimizationProfile(Enum):
    Performance = "Performance"
    Balanced = "Balanced"
    Storage = "Storage"


class RecommendationAction(Enum):
    Skip = "Skip"
    Compress = "Compress"


class RecommendationRisk(Enum):
    Low = "Low"
    Medium = "Medium"
    High = "High"


@dataclass
class CompressionRecommendation:
    action: RecommendationAction = RecommendationAction.Skip
    algorithm: CompressionAlgorithm | None = None
    risk: RecommendationRisk = RecommendationRisk.Low
    reasons: list[str] = field(default_factory=list)


def algorithm_to_string(algorithm: CompressionAlgorithm) -> str:
    return algorithm.value


def profile_to_string(profile: OptimizationProfile) -> str:
    return profile.value


def action_to_string(action: RecommendationAction) -> str:
    return action.value


def risk_to_string(risk: RecommendationRisk) -> str:
    return risk.value


def parse_compression_algorithm(value: str) -> CompressionAlgorithm | None:
    lower = value.lower()
    for algo in CompressionAlgorithm:
        if algo.value.lower() == lower:
            return algo
    return None


_GIBIBYTE = 1024 * 1024 * 1024

_ANTI_CHEAT_GAME_PATTERNS = [
    "apex legends", "fortnite", "valorant", "rainbow six",
    "call of duty", "pubg", "playerunknown", "destiny 2",
    "rust", "dead by daylight", "hunt: showdown", "war thunder",
    "escape from tarkov", "squad", "ark: survival", "battlefield",
    "counter-strike", "enlisted", "hell let loose",
    "insurgency: sandstorm", "lost ark", "new world", "paladins",
    "smite", "splitgate", "the finals", "warface", "for honor",
    "halo infinite", "naraka: bladepoint", "overwatch",
    "sea of thieves", "v rising", "brawlhalla", "crossout",
    "albion online", "dauntless",
]

_ANTI_CHEAT_PATH_NAMES = [
    "easyanticheat", "battleye", "faceit", "xigncode",
    "punkbuster", "vanguard", "equ8", "ricochet",
]


def _is_huge_game(total_bytes: int) -> bool:
    return total_bytes >= 50 * _GIBIBYTE


def _is_small_game(total_bytes: int) -> bool:
    return 0 < total_bytes <= 10 * _GIBIBYTE


def _is_anti_cheat_game_by_name(game_name: str) -> bool:
    lower = game_name.lower()
    return any(pattern in lower for pattern in _ANTI_CHEAT_GAME_PATTERNS)


def _is_anti_cheat_path(root_path: str) -> bool:
    lower = root_path.lower()
    return any(folder in lower for folder in _ANTI_CHEAT_PATH_NAMES)


def _apply_skip_rules(analysis, recommendation: CompressionRecommendation) -> bool:

    if not analysis.is_valid:
        recommendation.action = RecommendationAction.Skip
        recommendation.risk = RecommendationRisk.High
        recommendation.reasons.append("analysis-invalid")
        return True

    if analysis.total_bytes == 0 or analysis.file_count == 0:
        recommendation.action = RecommendationAction.Skip
        recommendation.risk = RecommendationRisk.Low
        recommendation.reasons.append("empty-folder")
        return True

    if analysis.already_compressed_byte_ratio() >= 0.88:
        recommendation.action = RecommendationAction.Skip
        recommendation.risk = RecommendationRisk.Medium
        recommendation.reasons.append("mostly-already-compressed-assets")
        return True

    if analysis.ntfs_compressed_byte_ratio() >= 0.95:
        recommendation.action = RecommendationAction.Skip
        recommendation.risk = RecommendationRisk.Low
        recommendation.reasons.append("already-ntfs-compressed")
        return True

    if analysis.total_bytes < analysis.logical_bytes * 0.50 and analysis.ntfs_compressed_byte_ratio() > 0.80:
        recommendation.action = RecommendationAction.Skip
        recommendation.risk = RecommendationRisk.Low
        recommendation.reasons.append("already-optimized-disk")
        return True

    if analysis.game_name and _is_anti_cheat_game_by_name(analysis.game_name):
        recommendation.action = RecommendationAction.Skip
        recommendation.risk = RecommendationRisk.High
        recommendation.reasons.append("anti-cheat-game")
        return True

    if analysis.contains_anti_cheat_files:
        recommendation.action = RecommendationAction.Skip
        recommendation.risk = RecommendationRisk.High
        recommendation.reasons.append("anti-cheat-files-found")
        return True

    if _is_anti_cheat_path(analysis.root_path):
        recommendation.action = RecommendationAction.Skip
        recommendation.risk = RecommendationRisk.High
        recommendation.reasons.append("anti-cheat-path")
        return True

    return False


class RecommendationEngine:
    def recommend(self, analysis, profile: OptimizationProfile) -> CompressionRecommendation:
        recommendation = CompressionRecommendation()

        if _apply_skip_rules(analysis, recommendation):
            return recommendation

        recommendation.action = RecommendationAction.Compress

        if profile == OptimizationProfile.Performance:
            recommendation.algorithm = CompressionAlgorithm.Xpress4k
            recommendation.risk = RecommendationRisk.Low
            recommendation.reasons.append("performance-profile")
            return recommendation

        if profile == OptimizationProfile.Storage:
            if _is_huge_game(analysis.total_bytes):
                recommendation.algorithm = CompressionAlgorithm.Lzx
                recommendation.risk = RecommendationRisk.Medium
            else:
                recommendation.algorithm = CompressionAlgorithm.Xpress16k
                recommendation.risk = RecommendationRisk.Low
            recommendation.reasons.append("storage-profile")
            return recommendation

        if _is_small_game(analysis.total_bytes):
            recommendation.algorithm = CompressionAlgorithm.Xpress4k
            recommendation.risk = RecommendationRisk.Low
            recommendation.reasons.append("small-or-light-game")
            return recommendation

        recommendation.algorithm = CompressionAlgorithm.Xpress8k
        recommendation.risk = RecommendationRisk.Low
        if _is_huge_game(analysis.total_bytes):
            recommendation.reasons.append("modern-large-game-balanced-default")
        else:
            recommendation.reasons.append("balanced-default")
        return recommendation

    def recommend_with_algorithm(self, analysis, algorithm: CompressionAlgorithm) -> CompressionRecommendation:
        recommendation = CompressionRecommendation()

        if _apply_skip_rules(analysis, recommendation):
            return recommendation

        recommendation.action = RecommendationAction.Compress
        recommendation.algorithm = algorithm
        recommendation.risk = RecommendationRisk.Low
        recommendation.reasons.append("manual-algorithm-selection")
        return recommendation
