#include "core/rules_engine/RecommendationEngine.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace gsm::core {
namespace {

constexpr std::uintmax_t gibibyte = 1024ULL * 1024ULL * 1024ULL;

bool isHugeGame(const GameAnalysis& analysis)
{
    return analysis.totalBytes >= 50ULL * gibibyte;
}

bool isSmallGame(const GameAnalysis& analysis)
{
    return analysis.totalBytes > 0 && analysis.totalBytes <= 10ULL * gibibyte;
}

std::string normalized(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool isAntiCheatGameByName(const std::string& gameName)
{
    static const std::vector<std::string> patterns = {
        "apex legends",
        "fortnite",
        "valorant",
        "rainbow six",
        "call of duty",
        "pubg",
        "playerunknown",
        "destiny 2",
        "rust",
        "dead by daylight",
        "hunt: showdown",
        "war thunder",
        "escape from tarkov",
        "squad",
        "ark: survival",
        "battlefield",
        "counter-strike",
        "enlisted",
        "hell let loose",
        "insurgency: sandstorm",
        "lost ark",
        "new world",
        "paladins",
        "smite",
        "splitgate",
        "the finals",
        "warface",
        "for honor",
        "halo infinite",
        "naraka: bladepoint",
        "overwatch",
        "sea of thieves",
        "v rising",
        "brawlhalla",
        "crossout",
        "albion online",
        "dauntless",
    };

    const std::string lower = normalized(gameName);
    for (const auto& pattern : patterns) {
        if (lower.find(pattern) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool isAntiCheatPath(const std::string& rootPath)
{
    static const std::vector<std::string> acFolders = {
        "easyanticheat",
        "battleye",
        "faceit",
        "xigncode",
        "punkbuster",
        "vanguard",
        "equ8",
        "ricochet",
    };

    const std::string lower = normalized(rootPath);
    for (const auto& folder : acFolders) {
        if (lower.find(folder) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

CompressionRecommendation RecommendationEngine::recommend(const GameAnalysis& analysis, OptimizationProfile profile) const
{
    CompressionRecommendation recommendation;

    if (!analysis.isValid) {
        recommendation.action = RecommendationAction::Skip;
        recommendation.risk = RecommendationRisk::High;
        recommendation.reasons.push_back("analysis-invalid");
        return recommendation;
    }

    if (analysis.totalBytes == 0 || analysis.fileCount == 0) {
        recommendation.action = RecommendationAction::Skip;
        recommendation.risk = RecommendationRisk::Low;
        recommendation.reasons.push_back("empty-folder");
        return recommendation;
    }

    if (analysis.alreadyCompressedByteRatio() >= 0.88) {
        recommendation.action = RecommendationAction::Skip;
        recommendation.risk = RecommendationRisk::Medium;
        recommendation.reasons.push_back("mostly-already-compressed-assets");
        return recommendation;
    }

    if (analysis.ntfsCompressedByteRatio() >= 0.90) {
        recommendation.action = RecommendationAction::Skip;
        recommendation.risk = RecommendationRisk::Medium;
        recommendation.reasons.push_back("already-ntfs-compressed");
        return recommendation;
    }

    if (!analysis.gameName.empty() && isAntiCheatGameByName(analysis.gameName)) {
        recommendation.action = RecommendationAction::Skip;
        recommendation.risk = RecommendationRisk::High;
        recommendation.reasons.push_back("anti-cheat-game");
        return recommendation;
    }

    if (analysis.containsAntiCheatFiles) {
        recommendation.action = RecommendationAction::Skip;
        recommendation.risk = RecommendationRisk::High;
        recommendation.reasons.push_back("anti-cheat-files-found");
        return recommendation;
    }

    if (isAntiCheatPath(analysis.rootPath)) {
        recommendation.action = RecommendationAction::Skip;
        recommendation.risk = RecommendationRisk::High;
        recommendation.reasons.push_back("anti-cheat-path");
        return recommendation;
    }

    recommendation.action = RecommendationAction::Compress;

    if (profile == OptimizationProfile::Performance) {
        recommendation.algorithm = CompressionAlgorithm::Xpress4k;
        recommendation.risk = RecommendationRisk::Low;
        recommendation.reasons.push_back("performance-profile");
        return recommendation;
    }

    if (profile == OptimizationProfile::Storage) {
        recommendation.algorithm = isHugeGame(analysis) ? CompressionAlgorithm::Lzx : CompressionAlgorithm::Xpress16k;
        recommendation.risk = isHugeGame(analysis) ? RecommendationRisk::Medium : RecommendationRisk::Low;
        recommendation.reasons.push_back("storage-profile");
        return recommendation;
    }

    if (isSmallGame(analysis)) {
        recommendation.algorithm = CompressionAlgorithm::Xpress4k;
        recommendation.risk = RecommendationRisk::Low;
        recommendation.reasons.push_back("small-or-light-game");
        return recommendation;
    }

    if (isHugeGame(analysis)) {
        recommendation.algorithm = CompressionAlgorithm::Xpress8k;
        recommendation.risk = RecommendationRisk::Low;
        recommendation.reasons.push_back("modern-large-game-balanced-default");
        return recommendation;
    }

    recommendation.algorithm = CompressionAlgorithm::Xpress8k;
    recommendation.risk = RecommendationRisk::Low;
    recommendation.reasons.push_back("balanced-default");
    return recommendation;
}

CompressionRecommendation RecommendationEngine::recommendWithAlgorithm(const GameAnalysis& analysis, CompressionAlgorithm algorithm) const
{
    CompressionRecommendation recommendation;

    if (!analysis.isValid) {
        recommendation.action = RecommendationAction::Skip;
        recommendation.risk = RecommendationRisk::High;
        recommendation.reasons.push_back("analysis-invalid");
        return recommendation;
    }

    if (analysis.totalBytes == 0 || analysis.fileCount == 0) {
        recommendation.action = RecommendationAction::Skip;
        recommendation.risk = RecommendationRisk::Low;
        recommendation.reasons.push_back("empty-folder");
        return recommendation;
    }

    if (analysis.alreadyCompressedByteRatio() >= 0.88) {
        recommendation.action = RecommendationAction::Skip;
        recommendation.risk = RecommendationRisk::Medium;
        recommendation.reasons.push_back("mostly-already-compressed-assets");
        return recommendation;
    }

    if (analysis.ntfsCompressedByteRatio() >= 0.90) {
        recommendation.action = RecommendationAction::Skip;
        recommendation.risk = RecommendationRisk::Medium;
        recommendation.reasons.push_back("already-ntfs-compressed");
        return recommendation;
    }

    if (!analysis.gameName.empty() && isAntiCheatGameByName(analysis.gameName)) {
        recommendation.action = RecommendationAction::Skip;
        recommendation.risk = RecommendationRisk::High;
        recommendation.reasons.push_back("anti-cheat-game");
        return recommendation;
    }

    if (analysis.containsAntiCheatFiles) {
        recommendation.action = RecommendationAction::Skip;
        recommendation.risk = RecommendationRisk::High;
        recommendation.reasons.push_back("anti-cheat-files-found");
        return recommendation;
    }

    if (isAntiCheatPath(analysis.rootPath)) {
        recommendation.action = RecommendationAction::Skip;
        recommendation.risk = RecommendationRisk::High;
        recommendation.reasons.push_back("anti-cheat-path");
        return recommendation;
    }

    recommendation.action = RecommendationAction::Compress;
    recommendation.algorithm = algorithm;
    recommendation.risk = RecommendationRisk::Low;
    recommendation.reasons.push_back("manual-algorithm-selection");
    return recommendation;
}

std::string toString(CompressionAlgorithm algorithm)
{
    switch (algorithm) {
    case CompressionAlgorithm::Xpress4k:
        return "XPRESS4K";
    case CompressionAlgorithm::Xpress8k:
        return "XPRESS8K";
    case CompressionAlgorithm::Xpress16k:
        return "XPRESS16K";
    case CompressionAlgorithm::Lzx:
        return "LZX";
    }

    return "UNKNOWN";
}

std::string toString(OptimizationProfile profile)
{
    switch (profile) {
    case OptimizationProfile::Performance:
        return "Performance";
    case OptimizationProfile::Balanced:
        return "Balanced";
    case OptimizationProfile::Storage:
        return "Storage";
    }

    return "Unknown";
}

std::string toString(RecommendationAction action)
{
    switch (action) {
    case RecommendationAction::Skip:
        return "Skip";
    case RecommendationAction::Compress:
        return "Compress";
    }

    return "Unknown";
}

std::string toString(RecommendationRisk risk)
{
    switch (risk) {
    case RecommendationRisk::Low:
        return "Low";
    case RecommendationRisk::Medium:
        return "Medium";
    case RecommendationRisk::High:
        return "High";
    }

    return "Unknown";
}

std::optional<CompressionAlgorithm> parseCompressionAlgorithm(const std::string& value)
{
    const std::string lower = normalized(value);

    if (lower == "xpress4k") {
        return CompressionAlgorithm::Xpress4k;
    }
    if (lower == "xpress8k") {
        return CompressionAlgorithm::Xpress8k;
    }
    if (lower == "xpress16k") {
        return CompressionAlgorithm::Xpress16k;
    }
    if (lower == "lzx") {
        return CompressionAlgorithm::Lzx;
    }

    return std::nullopt;
}

} // namespace gsm::core

