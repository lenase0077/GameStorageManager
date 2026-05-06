#pragma once

#include "core/rules_engine/RecommendationEngine.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gsm::core {

enum class SafetyOperationState {
    Planned,
    Running,
    Completed,
    Failed,
    Restored
};

struct SafetyMetadata {
    int schemaVersion = 1;
    std::string id;
    std::string gameName;
    std::string rootPath;
    std::string source;
    std::uintmax_t sizeBeforeBytes = 0;
    std::uintmax_t sizeAfterBytes = 0;
    std::uint64_t fileCountBefore = 0;
    std::optional<CompressionAlgorithm> algorithm;
    SafetyOperationState state = SafetyOperationState::Planned;
    std::string createdAtUtc;
    std::string updatedAtUtc;
    std::vector<std::string> notes;
};

std::string toString(SafetyOperationState state);
std::optional<SafetyOperationState> parseSafetyOperationState(const std::string& value);

} // namespace gsm::core

