#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gsm::core {

struct ExtensionStats {
    std::string extension;
    std::uint64_t fileCount = 0;
    std::uintmax_t totalBytes = 0;
};

struct GameAnalysis {
    bool isValid = false;
    std::string rootPath;
    std::string gameName;
    std::string errorMessage;

    std::uintmax_t totalBytes = 0;
    std::uint64_t fileCount = 0;
    std::uint64_t directoryCount = 0;
    std::uint64_t inaccessibleEntryCount = 0;

    std::uint64_t alreadyCompressedFileCount = 0;
    std::uintmax_t alreadyCompressedBytes = 0;
    std::uintmax_t largestFileBytes = 0;

    std::uint64_t ntfsCompressedFileCount = 0;
    std::uintmax_t ntfsCompressedBytes = 0;

    bool containsAntiCheatFiles = false;

    std::vector<ExtensionStats> extensions;

    double alreadyCompressedByteRatio() const;
    double ntfsCompressedByteRatio() const;
};

} // namespace gsm::core

