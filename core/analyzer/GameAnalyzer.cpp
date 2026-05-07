#include "core/analyzer/GameAnalyzer.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <system_error>
#include <unordered_set>
#include <filesystem>
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
    static const std::vector<std::string> acNames = {
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

void analyzeDirectory(
    const gsm::system::Path& directory,
    GameAnalysis& analysis,
    std::map<std::string, ExtensionStats>& extensionStats,
    std::atomic<bool>* cancelFlag)
{
    std::error_code ec;

    if (!std::filesystem::is_directory(directory, ec)) {
        return;
    }

    auto opts = std::filesystem::directory_options::skip_permission_denied;
    auto it = std::filesystem::recursive_directory_iterator(directory, opts, ec);
    if (ec) {
        ++analysis.inaccessibleEntryCount;
        return;
    }

    // Process the root directory first? No, just iterate
    for (auto& entry : it) {
        if (cancelFlag && cancelFlag->load()) {
            analysis.isValid = false;
            analysis.errorMessage = "Analysis cancelled by user.";
            return;
        }

        if (ec) {
            ++analysis.inaccessibleEntryCount;
            // Clear the error to continue
            ec.clear();
            continue;
        }

        const std::string fileName = entry.path().filename().string();
        
        if (entry.is_directory(ec)) {
            ++analysis.directoryCount;
            if (isAntiCheatFileName(fileName)) {
                analysis.containsAntiCheatFiles = true;
            }
            continue;
        }

        if (entry.is_regular_file(ec)) {
            std::uintmax_t size = 0;
            std::uintmax_t logSize = entry.file_size(ec);
            DWORD high = 0;
            DWORD low = GetCompressedFileSizeW(entry.path().wstring().c_str(), &high);
            if (low != INVALID_FILE_SIZE || GetLastError() == NO_ERROR) {
                size = (static_cast<std::uintmax_t>(high) << 32) | low;
            } else {
                size = logSize;
            }

            ++analysis.fileCount;
            analysis.totalBytes += size;
            analysis.logicalBytes += logSize;
            analysis.largestFileBytes = std::max(analysis.largestFileBytes, size);

            DWORD attributes = GetFileAttributesW(entry.path().wstring().c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_COMPRESSED) != 0) {
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
        }
    }
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

GameAnalysis GameAnalyzer::analyze(const gsm::system::Path& rootPath, const std::string& gameName, std::atomic<bool>* cancelFlag) const
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
    analyzeDirectory(analysis.rootPath, analysis, extensionStats, cancelFlag);

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
