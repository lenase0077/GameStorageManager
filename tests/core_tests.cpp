#include "core/analyzer/GameAnalysis.h"
#include "core/rules_engine/RecommendationEngine.h"
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

} // namespace

int main()
{
    balancedLargeGameUsesXpress8k();
    mostlyCompressedAssetsAreSkipped();
    compactCommandBuilderQuotesPaths();
    restoreCommandBuilderUsesUncompress();

    std::cout << "gsm_core_tests passed\n";
    return 0;
}

