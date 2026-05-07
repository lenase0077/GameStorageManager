#pragma once

#include "core/analyzer/GameAnalysis.h"
#include "system/filesystem/Filesystem.h"

#include <string>
#include <atomic>

namespace gsm::core {

class GameAnalyzer {
public:
    GameAnalysis analyze(const gsm::system::Path& rootPath, const std::string& gameName = "", std::atomic<bool>* cancelFlag = nullptr) const;
    static bool isKnownCompressedExtension(const std::string& extension);
};

} // namespace gsm::core
