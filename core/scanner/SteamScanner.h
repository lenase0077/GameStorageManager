#pragma once

#include "core/scanner/GameEntry.h"
#include "system/filesystem/Filesystem.h"

#include <optional>
#include <string>
#include <vector>

namespace gsm::core {

class SteamScanner {
public:
    std::vector<GameEntry> scanInstalledGames() const;
    std::vector<gsm::system::Path> discoverSteamRoots() const;
    std::vector<gsm::system::Path> discoverLibraryFolders(const gsm::system::Path& steamRoot) const;
    std::vector<GameEntry> scanLibrary(const gsm::system::Path& libraryPath) const;

    static std::vector<gsm::system::Path> parseLibraryFoldersVdf(
        const gsm::system::Path& steamRoot,
        const std::string& text);

    static std::optional<GameEntry> parseAppManifest(
        const gsm::system::Path& libraryPath,
        const std::string& text);
};

} // namespace gsm::core

