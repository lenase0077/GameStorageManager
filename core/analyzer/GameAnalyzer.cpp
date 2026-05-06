#include "core/analyzer/GameAnalyzer.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <system_error>
#include <unordered_set>
#include <windows.h>

namespace gsm::core {
namespace {

std::string lowercased(const std::string& value)
{
    std::string result = value;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

bool isAntiCheatFileName(const std::string& fileName)
{
    static const std::unordered_set<std::string> acNames = {
        "battleye", "belauncher", "beservice", "battleye.sys",
        "easyanticheat", "easyanticheat.sys",
        "vgk", "vgk.sys", "vanguard",
        "faceit", "faceit.sys",
        "equ8", "equ8.dll",
        "xigncode", "x3.xem", "xmag.xem",
        "punkbuster", "pbsvc", "pbsetup", "pbcl",
        "wellbia", "npgg", "uncheater",
        "ricochet",
        "arbiter.sys", "arbiter",
        "tencentprotect",
        "sguard",
        "mrac", "mrac.sys",
        "gameguard",
        "hackshield",
        "nexonguard",
        "blackcipher",
    };

    const std::string lower = lowercased(fileName);
    for (const auto& ac : acNames) {
        if (lower == ac) return true;
        if (lower.find(ac) != std::string::npos) return true;
    }
    return false;
}

std::string normalizeExtension(const std::string& fileName)
{
    std::string extension = gsm::system::fileExtension(fileName);
    if (extension.empty()) {
        return "<none>";
    }

    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });

    return extension;
}

std::uintmax_t fileSizeFromFindData(const WIN32_FIND_DATAA& findData)
{
    ULARGE_INTEGER size;
    size.HighPart = findData.nFileSizeHigh;
    size.LowPart = findData.nFileSizeLow;
    return size.QuadPart;
}

void analyzeDirectory(
    const gsm::system::Path& directory,
    GameAnalysis& analysis,
    std::map<std::string, ExtensionStats>& extensionStats)
{
    const std::string searchPath = gsm::system::joinPath(directory, "*");
    WIN32_FIND_DATAA findData;
    HANDLE findHandle = FindFirstFileA(searchPath.c_str(), &findData);

    if (findHandle == INVALID_HANDLE_VALUE) {
        ++analysis.inaccessibleEntryCount;
        return;
    }

    do {
        const std::string fileName = findData.cFileName;
        if (fileName == "." || fileName == "..") {
            continue;
        }

        const bool isDirectory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const bool isReparsePoint = (findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        const std::string fullPath = gsm::system::joinPath(directory, fileName);

        if (isDirectory) {
            ++analysis.directoryCount;
            if (isAntiCheatFileName(fileName)) {
                analysis.containsAntiCheatFiles = true;
            }
            if (!isReparsePoint) {
                analyzeDirectory(fullPath, analysis, extensionStats);
            }
            continue;
        }

        const std::uintmax_t size = fileSizeFromFindData(findData);
        ++analysis.fileCount;
        analysis.totalBytes += size;
        analysis.largestFileBytes = std::max(analysis.largestFileBytes, size);

        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED) != 0) {
            ++analysis.ntfsCompressedFileCount;
            analysis.ntfsCompressedBytes += size;
        }

        const std::string extension = normalizeExtension(fileName);
        ExtensionStats& stats = extensionStats[extension];
        stats.extension = extension;
        ++stats.fileCount;
        stats.totalBytes += size;

        if (GameAnalyzer::isKnownCompressedExtension(extension)) {
            ++analysis.alreadyCompressedFileCount;
            analysis.alreadyCompressedBytes += size;
        }

        if (!analysis.containsAntiCheatFiles && isAntiCheatFileName(fileName)) {
            analysis.containsAntiCheatFiles = true;
        }
    } while (FindNextFileA(findHandle, &findData) != 0);

    if (GetLastError() != ERROR_NO_MORE_FILES) {
        ++analysis.inaccessibleEntryCount;
    }

    FindClose(findHandle);
}

} // namespace

double GameAnalysis::alreadyCompressedByteRatio() const
{
    if (totalBytes == 0) {
        return 0.0;
    }

    return static_cast<double>(alreadyCompressedBytes) / static_cast<double>(totalBytes);
}

double GameAnalysis::ntfsCompressedByteRatio() const
{
    if (totalBytes == 0) {
        return 0.0;
    }

    return static_cast<double>(ntfsCompressedBytes) / static_cast<double>(totalBytes);
}

GameAnalysis GameAnalyzer::analyze(const gsm::system::Path& rootPath, const std::string& gameName) const
{
    GameAnalysis analysis;
    analysis.rootPath = gsm::system::normalizePath(rootPath);
    analysis.gameName = gameName;

    if (!gsm::system::directoryExists(analysis.rootPath)) {
        analysis.errorMessage = "Path does not exist.";
        return analysis;
    }

    analysis.isValid = true;
    std::map<std::string, ExtensionStats> extensionStats;
    analyzeDirectory(analysis.rootPath, analysis, extensionStats);

    analysis.extensions.reserve(extensionStats.size());
    for (const auto& pair : extensionStats) {
        analysis.extensions.push_back(pair.second);
    }

    std::sort(analysis.extensions.begin(), analysis.extensions.end(), [](const ExtensionStats& left, const ExtensionStats& right) {
        return left.totalBytes > right.totalBytes;
    });

    return analysis;
}

bool GameAnalyzer::isKnownCompressedExtension(const std::string& extension)
{
    static const std::unordered_set<std::string> compressedExtensions = {
        ".7z", ".arc", ".bik", ".bnk", ".cab", ".dds", ".gz", ".jpg", ".jpeg", ".mp3",
        ".mp4", ".ogg", ".png", ".rar", ".usm", ".wem", ".zip", ".zst"
    };

    return compressedExtensions.find(extension) != compressedExtensions.end();
}

} // namespace gsm::core
