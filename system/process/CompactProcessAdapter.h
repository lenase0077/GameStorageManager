#pragma once

#include "core/rules_engine/RecommendationEngine.h"
#include "system/filesystem/Filesystem.h"

#include <string>
#include <vector>

namespace gsm::system {

struct CompactCommand {
    std::string executable = "compact.exe";
    std::vector<std::string> arguments;
};

struct ProcessResult {
    int exitCode = -1;
    std::string output;
};

struct CompactOutputMetrics {
    bool parsed = false;
    std::uintmax_t bytesBeforeFromOutput = 0;
    std::uintmax_t bytesAfterFromOutput = 0;
    std::uint64_t filesProcessed = 0;
    std::uint64_t filesAlreadyCompressed = 0;
    std::uint64_t filesCompressed = 0;
};

class CompactProcessAdapter {
public:
    CompactCommand buildCompressCommand(
        const gsm::system::Path& targetPath,
        gsm::core::CompressionAlgorithm algorithm) const;

    CompactCommand buildRestoreCommand(const gsm::system::Path& targetPath) const;

    ProcessResult run(const CompactCommand& command) const;

    static CompactOutputMetrics parseCompressOutput(const std::string& output);
};

std::string toDisplayString(const CompactCommand& command);

} // namespace gsm::system
