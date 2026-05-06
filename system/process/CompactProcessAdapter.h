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

class CompactProcessAdapter {
public:
    CompactCommand buildCompressCommand(
        const gsm::system::Path& targetPath,
        gsm::core::CompressionAlgorithm algorithm) const;

    CompactCommand buildRestoreCommand(const gsm::system::Path& targetPath) const;

    ProcessResult run(const CompactCommand& command) const;
};

std::string toDisplayString(const CompactCommand& command);

} // namespace gsm::system
