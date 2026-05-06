#pragma once

#include <cstdint>
#include <string>

namespace gsm::core {

enum class GameSource {
    Manual,
    Steam,
    Epic
};

struct GameEntry {
    GameSource source = GameSource::Manual;
    std::string sourceId;
    std::string name;
    std::string installPath;
    std::string libraryPath;
    std::uintmax_t sizeBytes = 0;
    bool installed = true;
};

std::string toString(GameSource source);

} // namespace gsm::core

