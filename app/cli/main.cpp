#include "core/analyzer/GameAnalyzer.h"
#include "core/rules_engine/RecommendationEngine.h"
#include "core/scanner/SteamScanner.h"
#include "system/filesystem/Filesystem.h"
#include "system/process/CompactProcessAdapter.h"

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace {

std::string formatBytes(std::uintmax_t bytes)
{
    constexpr double kib = 1024.0;
    constexpr double mib = kib * 1024.0;
    constexpr double gib = mib * 1024.0;

    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2);

    if (bytes >= static_cast<std::uintmax_t>(gib)) {
        stream << static_cast<double>(bytes) / gib << " GB";
        return stream.str();
    }
    if (bytes >= static_cast<std::uintmax_t>(mib)) {
        stream << static_cast<double>(bytes) / mib << " MB";
        return stream.str();
    }
    if (bytes >= static_cast<std::uintmax_t>(kib)) {
        stream << static_cast<double>(bytes) / kib << " KB";
        return stream.str();
    }
    return std::to_string(bytes) + " B";
}

void printUsage()
{
    std::cout
        << "Game Storage Manager CLI\n"
        << "\n"
        << "Usage:\n"
        << "  gsm_cli analyze <folder>\n"
        << "  gsm_cli scan-steam\n"
        << "  gsm_cli compact-command <compress|restore> <folder> [xpress4k|xpress8k|xpress16k|lzx]\n";
}

void printRecommendation(const gsm::core::CompressionRecommendation& recommendation)
{
    std::cout << "Recommendation: " << gsm::core::toString(recommendation.action) << "\n";

    if (recommendation.algorithm.has_value()) {
        std::cout << "Algorithm: " << gsm::core::toString(*recommendation.algorithm) << "\n";
    }

    std::cout << "Risk: " << gsm::core::toString(recommendation.risk) << "\n";

    if (!recommendation.reasons.empty()) {
        std::cout << "Reasons:\n";
        for (const std::string& reason : recommendation.reasons) {
            std::cout << "  - " << reason << "\n";
        }
    }
}

int analyzeFolder(const gsm::system::Path& folder)
{
    gsm::core::GameAnalyzer analyzer;
    const gsm::core::GameAnalysis analysis = analyzer.analyze(folder);

    if (!analysis.isValid) {
        std::cerr << "Invalid folder: " << analysis.errorMessage << "\n";
        return 2;
    }

    std::cout << "Path: " << analysis.rootPath << "\n";
    std::cout << "Total size: " << formatBytes(analysis.totalBytes) << "\n";
    std::cout << "Files: " << analysis.fileCount << "\n";
    std::cout << "Directories: " << analysis.directoryCount << "\n";
    std::cout << "Already-compressed assets: " << analysis.alreadyCompressedFileCount << "\n";
    std::cout << "Largest file: " << formatBytes(analysis.largestFileBytes) << "\n";

    if (analysis.inaccessibleEntryCount > 0) {
        std::cout << "Inaccessible entries: " << analysis.inaccessibleEntryCount << "\n";
    }

    if (!analysis.extensions.empty()) {
        std::cout << "Top extensions:\n";
        const std::size_t maxItems = std::min<std::size_t>(analysis.extensions.size(), 8);
        for (std::size_t index = 0; index < maxItems; ++index) {
            const gsm::core::ExtensionStats& item = analysis.extensions[index];
            std::cout << "  " << item.extension << ": " << item.fileCount
                      << " files, " << formatBytes(item.totalBytes) << "\n";
        }
    }

    gsm::core::RecommendationEngine engine;
    const gsm::core::CompressionRecommendation recommendation =
        engine.recommend(analysis, gsm::core::OptimizationProfile::Balanced);

    std::cout << "\n";
    printRecommendation(recommendation);

    return 0;
}

int printCompactCommand(int argc, char* argv[])
{
    if (argc < 4) {
        printUsage();
        return 1;
    }

    const std::string operation = argv[2];
    const gsm::system::Path folder = argv[3];
    gsm::system::CompactProcessAdapter adapter;

    if (operation == "restore") {
        const gsm::system::CompactCommand command = adapter.buildRestoreCommand(folder);
        std::cout << gsm::system::toDisplayString(command) << "\n";
        return 0;
    }

    if (operation != "compress") {
        std::cerr << "Unknown operation: " << operation << "\n";
        return 1;
    }

    gsm::core::CompressionAlgorithm algorithm = gsm::core::CompressionAlgorithm::Xpress8k;
    if (argc >= 5) {
        const auto parsed = gsm::core::parseCompressionAlgorithm(argv[4]);
        if (!parsed.has_value()) {
            std::cerr << "Unknown algorithm: " << argv[4] << "\n";
            return 1;
        }
        algorithm = *parsed;
    }

    const gsm::system::CompactCommand command = adapter.buildCompressCommand(folder, algorithm);
    std::cout << gsm::system::toDisplayString(command) << "\n";
    return 0;
}

int scanSteam()
{
    gsm::core::SteamScanner scanner;
    const std::vector<gsm::core::GameEntry> games = scanner.scanInstalledGames();

    std::cout << "Steam games found: " << games.size() << "\n";

    for (const gsm::core::GameEntry& game : games) {
        std::cout << "- [" << game.sourceId << "] " << game.name << "\n";
        std::cout << "  Library: " << game.libraryPath << "\n";
        std::cout << "  Path: " << game.installPath << "\n";
    }

    return 0;
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc < 2) {
        printUsage();
        return 1;
    }

    const std::string command = argv[1];

    if (command == "analyze") {
        if (argc < 3) {
            printUsage();
            return 1;
        }
        return analyzeFolder(argv[2]);
    }

    if (command == "compact-command") {
        return printCompactCommand(argc, argv);
    }

    if (command == "scan-steam") {
        return scanSteam();
    }

    printUsage();
    return 1;
}
