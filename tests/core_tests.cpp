#include "core/analyzer/GameAnalysis.h"
#include "core/rules_engine/RecommendationEngine.h"
#include "core/safety/SafetyMetadataStore.h"
#include "system/process/CompactProcessAdapter.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

constexpr std::uintmax_t gibibyte = 1024ULL * 1024ULL * 1024ULL;

gsm::core::GameAnalysis makeAnalysis(std::uintmax_t totalBytes, std::uintmax_t compressedBytes)
{
    gsm::core::GameAnalysis analysis;
    analysis.isValid = true;
    analysis.rootPath = "C:\\Games\\Example";
    analysis.totalBytes = totalBytes;
    analysis.fileCount = 100;
    analysis.alreadyCompressedBytes = compressedBytes;
    analysis.alreadyCompressedFileCount = compressedBytes > 0 ? 80 : 0;
    return analysis;
}

void balancedLargeGameUsesXpress8k()
{
    const gsm::core::GameAnalysis analysis = makeAnalysis(60ULL * gibibyte, 10ULL * gibibyte);
    gsm::core::RecommendationEngine engine;

    const gsm::core::CompressionRecommendation recommendation =
        engine.recommend(analysis, gsm::core::OptimizationProfile::Balanced);

    assert(recommendation.action == gsm::core::RecommendationAction::Compress);
    assert(recommendation.algorithm.has_value());
    assert(*recommendation.algorithm == gsm::core::CompressionAlgorithm::Xpress8k);
}

void mostlyCompressedAssetsAreSkipped()
{
    const gsm::core::GameAnalysis analysis = makeAnalysis(10ULL * gibibyte, 8ULL * gibibyte);
    gsm::core::RecommendationEngine engine;

    const gsm::core::CompressionRecommendation recommendation =
        engine.recommend(analysis, gsm::core::OptimizationProfile::Balanced);

    assert(recommendation.action == gsm::core::RecommendationAction::Skip);
    assert(!recommendation.algorithm.has_value());
    assert(recommendation.risk == gsm::core::RecommendationRisk::Medium);
}

void compactCommandBuilderQuotesPaths()
{
    gsm::system::CompactProcessAdapter adapter;
    const gsm::system::CompactCommand command =
        adapter.buildCompressCommand("C:\\Games\\Example Game", gsm::core::CompressionAlgorithm::Lzx);

    const std::string display = gsm::system::toDisplayString(command);
    assert(display == "compact.exe /c /s /a /i /exe:LZX \"C:\\Games\\Example Game\"");
}

void restoreCommandBuilderUsesUncompress()
{
    gsm::system::CompactProcessAdapter adapter;
    const gsm::system::CompactCommand command = adapter.buildRestoreCommand("C:\\Games\\Example Game");

    const std::string display = gsm::system::toDisplayString(command);
    assert(display == "compact.exe /u /s \"C:\\Games\\Example Game\"");
}

void safetyMetadataRoundTrips()
{
    gsm::core::SafetyMetadata metadata;
    metadata.id = "sample-id";
    metadata.gameName = "Example | Game";
    metadata.rootPath = "D:\\SteamLibrary\\steamapps\\common\\Example Game";
    metadata.source = "steam";
    metadata.sizeBeforeBytes = 123456;
    metadata.sizeAfterBytes = 100000;
    metadata.fileCountBefore = 42;
    metadata.algorithm = gsm::core::CompressionAlgorithm::Xpress8k;
    metadata.state = gsm::core::SafetyOperationState::Planned;
    metadata.createdAtUtc = "2026-05-06T00:00:00Z";
    metadata.updatedAtUtc = "2026-05-06T00:01:00Z";
    metadata.notes = {"recommendation:balanced-default", "path can contain | and \\ characters"};

    const std::string text = gsm::core::serializeSafetyMetadata(metadata);
    const std::optional<gsm::core::SafetyMetadata> parsed = gsm::core::deserializeSafetyMetadata(text);

    assert(parsed.has_value());
    assert(parsed->id == metadata.id);
    assert(parsed->gameName == metadata.gameName);
    assert(parsed->rootPath == metadata.rootPath);
    assert(parsed->source == metadata.source);
    assert(parsed->sizeBeforeBytes == metadata.sizeBeforeBytes);
    assert(parsed->sizeAfterBytes == metadata.sizeAfterBytes);
    assert(parsed->fileCountBefore == metadata.fileCountBefore);
    assert(parsed->algorithm.has_value());
    assert(*parsed->algorithm == gsm::core::CompressionAlgorithm::Xpress8k);
    assert(parsed->state == gsm::core::SafetyOperationState::Planned);
    assert(parsed->notes == metadata.notes);
}

void safetyMetadataStoreSavesAndLoads()
{
    const gsm::core::GameAnalysis analysis = makeAnalysis(12ULL * gibibyte, 2ULL * gibibyte);
    gsm::core::RecommendationEngine engine;
    const gsm::core::CompressionRecommendation recommendation =
        engine.recommend(analysis, gsm::core::OptimizationProfile::Balanced);

    gsm::core::SafetyMetadataStore store(".\\gsm_safety_test_metadata");
    gsm::core::SafetyMetadata metadata =
        store.createPlannedMetadata(analysis, recommendation, "Example Game", "manual");

    assert(!metadata.id.empty());
    assert(metadata.rootPath == "C:\\Games\\Example");
    assert(metadata.gameName == "Example Game");
    assert(metadata.source == "manual");
    assert(metadata.sizeBeforeBytes == analysis.totalBytes);
    assert(metadata.algorithm.has_value());

    assert(store.save(metadata));

    const std::optional<gsm::core::SafetyMetadata> loaded = store.loadById(metadata.id);
    assert(loaded.has_value());
    assert(loaded->id == metadata.id);
    assert(loaded->rootPath == metadata.rootPath);
    assert(loaded->algorithm == metadata.algorithm);
}

void stableIdsNormalizeDrivePaths()
{
    const std::string first = gsm::core::SafetyMetadataStore::makeStableId("D:/SteamLibrary/steamapps/common/Game");
    const std::string second = gsm::core::SafetyMetadataStore::makeStableId("d:\\SteamLibrary\\steamapps\\common\\Game\\");

    assert(first == second);
}

} // namespace

int main()
{
    balancedLargeGameUsesXpress8k();
    mostlyCompressedAssetsAreSkipped();
    compactCommandBuilderQuotesPaths();
    restoreCommandBuilderUsesUncompress();
    safetyMetadataRoundTrips();
    safetyMetadataStoreSavesAndLoads();
    stableIdsNormalizeDrivePaths();

    std::cout << "gsm_core_tests passed\n";
    return 0;
}
