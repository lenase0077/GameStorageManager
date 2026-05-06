#include "core/scanner/SteamScanner.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <windows.h>

namespace gsm::core {
namespace {

std::string readTextFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string registryString(HKEY root, const char* subKey, const char* valueName)
{
    char buffer[4096]{};
    DWORD bufferSize = sizeof(buffer);
    const LONG result = RegGetValueA(
        root,
        subKey,
        valueName,
        RRF_RT_REG_SZ,
        nullptr,
        buffer,
        &bufferSize);

    if (result != ERROR_SUCCESS || bufferSize == 0) {
        return {};
    }

    return std::string(buffer);
}

std::optional<std::string> environmentVariable(const char* name)
{
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return std::nullopt;
    }
    return std::string(value);
}

bool isNumeric(const std::string& value)
{
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isdigit(character) != 0;
    });
}

std::string toLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string unescapeVdfString(const std::string& value)
{
    std::string result;
    result.reserve(value.size());

    for (std::size_t index = 0; index < value.size(); ++index) {
        const char character = value[index];
        if (character != '\\' || index + 1 >= value.size()) {
            result += character;
            continue;
        }

        const char next = value[++index];
        switch (next) {
        case '\\':
            result += '\\';
            break;
        case '"':
            result += '"';
            break;
        case 'n':
            result += '\n';
            break;
        case 't':
            result += '\t';
            break;
        default:
            result += next;
            break;
        }
    }

    return result;
}

std::vector<std::string> tokenizeVdf(const std::string& text)
{
    std::vector<std::string> tokens;

    for (std::size_t index = 0; index < text.size();) {
        const char character = text[index];

        if (std::isspace(static_cast<unsigned char>(character)) != 0) {
            ++index;
            continue;
        }

        if (character == '/' && index + 1 < text.size() && text[index + 1] == '/') {
            while (index < text.size() && text[index] != '\n') {
                ++index;
            }
            continue;
        }

        if (character == '{' || character == '}') {
            tokens.emplace_back(1, character);
            ++index;
            continue;
        }

        if (character == '"') {
            ++index;
            std::string value;
            while (index < text.size()) {
                const char current = text[index++];
                if (current == '"') {
                    break;
                }
                if (current == '\\' && index < text.size()) {
                    value += current;
                    value += text[index++];
                    continue;
                }
                value += current;
            }
            tokens.push_back(unescapeVdfString(value));
            continue;
        }

        std::string bareToken;
        while (index < text.size()) {
            const char current = text[index];
            if (std::isspace(static_cast<unsigned char>(current)) != 0 || current == '{' || current == '}') {
                break;
            }
            bareToken += current;
            ++index;
        }
        tokens.push_back(bareToken);
    }

    return tokens;
}

void addUniquePath(std::vector<gsm::system::Path>& paths, const gsm::system::Path& path)
{
    const gsm::system::Path normalized = gsm::system::normalizePath(path);
    if (normalized.empty()) {
        return;
    }

    const std::string normalizedKey = toLower(normalized);
    const auto found = std::find_if(paths.begin(), paths.end(), [&normalizedKey](const gsm::system::Path& existing) {
        return toLower(gsm::system::normalizePath(existing)) == normalizedKey;
    });

    if (found == paths.end()) {
        paths.push_back(normalized);
    }
}

void addUniqueGame(std::vector<GameEntry>& games, const GameEntry& game)
{
    const std::string key = toLower(game.sourceId + "|" + gsm::system::normalizePath(game.installPath));
    const auto found = std::find_if(games.begin(), games.end(), [&key](const GameEntry& existing) {
        return toLower(existing.sourceId + "|" + gsm::system::normalizePath(existing.installPath)) == key;
    });

    if (found == games.end()) {
        games.push_back(game);
    }
}

} // namespace

std::string toString(GameSource source)
{
    switch (source) {
    case GameSource::Manual:
        return "manual";
    case GameSource::Steam:
        return "steam";
    case GameSource::Epic:
        return "epic";
    }

    return "unknown";
}

std::vector<GameEntry> SteamScanner::scanInstalledGames() const
{
    std::vector<GameEntry> games;
    std::vector<gsm::system::Path> libraries;

    for (const gsm::system::Path& steamRoot : discoverSteamRoots()) {
        for (const gsm::system::Path& library : discoverLibraryFolders(steamRoot)) {
            addUniquePath(libraries, library);
        }
    }

    for (const gsm::system::Path& library : libraries) {
        std::vector<GameEntry> libraryGames = scanLibrary(library);
        for (const GameEntry& game : libraryGames) {
            addUniqueGame(games, game);
        }
    }

    std::sort(games.begin(), games.end(), [](const GameEntry& left, const GameEntry& right) {
        return left.name < right.name;
    });

    return games;
}

std::vector<gsm::system::Path> SteamScanner::discoverSteamRoots() const
{
    std::vector<gsm::system::Path> roots;

    addUniquePath(roots, registryString(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath"));
    addUniquePath(roots, registryString(HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Valve\\Steam", "InstallPath"));
    addUniquePath(roots, registryString(HKEY_LOCAL_MACHINE, "SOFTWARE\\Valve\\Steam", "InstallPath"));

    if (const auto programFilesX86 = environmentVariable("ProgramFiles(x86)")) {
        addUniquePath(roots, gsm::system::joinPath(*programFilesX86, "Steam"));
    }

    if (const auto programFiles = environmentVariable("ProgramFiles")) {
        addUniquePath(roots, gsm::system::joinPath(*programFiles, "Steam"));
    }

    roots.erase(std::remove_if(roots.begin(), roots.end(), [](const gsm::system::Path& path) {
        return !gsm::system::directoryExists(path);
    }), roots.end());

    return roots;
}

std::vector<gsm::system::Path> SteamScanner::discoverLibraryFolders(const gsm::system::Path& steamRoot) const
{
    const gsm::system::Path libraryFoldersPath =
        gsm::system::joinPath(gsm::system::joinPath(steamRoot, "steamapps"), "libraryfolders.vdf");

    const std::string text = readTextFile(libraryFoldersPath);
    if (text.empty()) {
        return {};
    }

    return parseLibraryFoldersVdf(steamRoot, text);
}

std::vector<GameEntry> SteamScanner::scanLibrary(const gsm::system::Path& libraryPath) const
{
    const gsm::system::Path steamAppsPath = gsm::system::joinPath(libraryPath, "steamapps");
    std::vector<GameEntry> games;

    for (const gsm::system::Path& manifestPath : gsm::system::listFilesWithPrefixSuffix(steamAppsPath, "appmanifest_", ".acf")) {
        const std::string text = readTextFile(manifestPath);
        const std::optional<GameEntry> game = parseAppManifest(libraryPath, text);
        if (game.has_value()) {
            games.push_back(*game);
        }
    }

    return games;
}

std::vector<gsm::system::Path> SteamScanner::parseLibraryFoldersVdf(
    const gsm::system::Path& steamRoot,
    const std::string& text)
{
    std::vector<gsm::system::Path> libraries;
    addUniquePath(libraries, steamRoot);

    const std::vector<std::string> tokens = tokenizeVdf(text);

    for (std::size_t index = 0; index + 1 < tokens.size(); ++index) {
        const std::string& token = tokens[index];
        const std::string& next = tokens[index + 1];

        if (token == "path" && next != "{" && next != "}") {
            addUniquePath(libraries, next);
            continue;
        }

        if (isNumeric(token) && next != "{" && next != "}") {
            addUniquePath(libraries, next);
        }
    }

    return libraries;
}

std::optional<GameEntry> SteamScanner::parseAppManifest(
    const gsm::system::Path& libraryPath,
    const std::string& text)
{
    const std::vector<std::string> tokens = tokenizeVdf(text);

    std::string appId;
    std::string name;
    std::string installDir;

    for (std::size_t index = 0; index + 1 < tokens.size(); ++index) {
        const std::string& key = tokens[index];
        const std::string& value = tokens[index + 1];

        if (value == "{" || value == "}") {
            continue;
        }

        if (key == "appid") {
            appId = value;
        } else if (key == "name") {
            name = value;
        } else if (key == "installdir") {
            installDir = value;
        }
    }

    if (appId.empty() || name.empty() || installDir.empty()) {
        return std::nullopt;
    }

    GameEntry entry;
    entry.source = GameSource::Steam;
    entry.sourceId = appId;
    entry.name = name;
    entry.libraryPath = gsm::system::normalizePath(libraryPath);
    entry.installPath = gsm::system::joinPath(gsm::system::joinPath(entry.libraryPath, "steamapps\\common"), installDir);
    entry.installed = true;
    return entry;
}

} // namespace gsm::core
