#pragma once

#include "core/analyzer/GameAnalysis.h"

#include <optional>
#include <string>
#include <vector>

namespace gsm::core {

enum class CompressionAlgorithm {
    Xpress4k,
    Xpress8k,
    Xpress16k,
    Lzx
};

enum class OptimizationProfile {
    Performance,
    Balanced,
    Storage
};

enum class RecommendationAction {
    Skip,
    Compress
};

enum class RecommendationRisk {
    Low,
    Medium,
    High
};

struct CompressionRecommendation {
    RecommendationAction action = RecommendationAction::Skip;
    std::optional<CompressionAlgorithm> algorithm;
    RecommendationRisk risk = RecommendationRisk::Low;
    std::vector<std::string> reasons;
};

class RecommendationEngine {
public:
    CompressionRecommendation recommend(const GameAnalysis& analysis, OptimizationProfile profile) const;
};

std::string toString(CompressionAlgorithm algorithm);
std::string toString(OptimizationProfile profile);
std::string toString(RecommendationAction action);
std::string toString(RecommendationRisk risk);
std::optional<CompressionAlgorithm> parseCompressionAlgorithm(const std::string& value);

} // namespace gsm::core

