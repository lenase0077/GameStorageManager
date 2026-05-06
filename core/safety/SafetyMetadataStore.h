#pragma once

#include "core/analyzer/GameAnalysis.h"
#include "core/rules_engine/RecommendationEngine.h"
#include "core/safety/SafetyMetadata.h"
#include "system/filesystem/Filesystem.h"

#include <optional>
#include <string>
#include <vector>

namespace gsm::core {

class SafetyMetadataStore {
public:
    explicit SafetyMetadataStore(gsm::system::Path storageRoot);

    SafetyMetadata createPlannedMetadata(
        const GameAnalysis& analysis,
        const CompressionRecommendation& recommendation,
        const std::string& gameName,
        const std::string& source) const;

    bool save(const SafetyMetadata& metadata) const;
    std::optional<SafetyMetadata> loadById(const std::string& id) const;
    std::vector<SafetyMetadata> loadAll() const;

    gsm::system::Path storageRoot() const;
    gsm::system::Path metadataPathForId(const std::string& id) const;

    static std::string makeStableId(const std::string& rootPath);

private:
    gsm::system::Path storageRoot_;
};

std::string serializeSafetyMetadata(const SafetyMetadata& metadata);
std::optional<SafetyMetadata> deserializeSafetyMetadata(const std::string& text);

} // namespace gsm::core

