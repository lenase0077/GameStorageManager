#pragma once

#include "core/analyzer/GameAnalysis.h"
#include "core/rules_engine/RecommendationEngine.h"
#include "core/safety/SafetyMetadata.h"
#include "core/safety/SafetyMetadataStore.h"
#include "system/filesystem/Filesystem.h"
#include "system/process/CompactProcessAdapter.h"

#include <cstdint>
#include <string>
#include <functional>
#include <atomic>

namespace gsm::core {

struct CompressionResult {
    bool success = false;
    std::string errorMessage;
    std::uintmax_t bytesBefore = 0;
    std::uintmax_t bytesAfter = 0;
    int exitCode = -1;
    std::string output;
    gsm::system::CompactOutputMetrics metrics;
};

class Compressor {
public:

    CompressionResult compress(
        const GameAnalysis& analysis,
        const CompressionRecommendation& recommendation,
        SafetyMetadataStore& metadataStore,
        std::function<void(size_t)> onProgress = nullptr,
        std::atomic<bool>* cancelFlag = nullptr) const;

    CompressionResult restore(
        const SafetyMetadata& metadata,
        SafetyMetadataStore& metadataStore,
        const gsm::system::Path& targetPath,
        std::function<void(size_t)> onProgress = nullptr,
        std::atomic<bool>* cancelFlag = nullptr) const;

private:
    gsm::system::CompactProcessAdapter adapter_;
};

} // namespace gsm::core
