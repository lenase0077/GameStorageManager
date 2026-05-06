#include "system/filesystem/Filesystem.h"

#include <algorithm>
#include <filesystem>
#include <windows.h>

namespace gsm::system {

bool directoryExists(const Path& path)
{
    std::error_code ec;
    return std::filesystem::is_directory(path, ec);
}

bool ensureDirectoryExists(const Path& path)
{
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return std::filesystem::is_directory(path, ec);
}

bool fileExists(const Path& path)
{
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

std::vector<Path> listFilesWithPrefixSuffix(const Path& directory, const std::string& prefix, const std::string& suffix)
{
    std::vector<Path> files;
    std::error_code ec;

    auto it = std::filesystem::directory_iterator(directory, ec);
    if (ec) {
        return files;
    }

    for (const auto& entry : it) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }

        std::string fileName = entry.path().filename().string();
        
        bool matchesPrefix = prefix.empty() || (fileName.size() >= prefix.size() && fileName.substr(0, prefix.size()) == prefix);
        bool matchesSuffix = suffix.empty() || (fileName.size() >= suffix.size() && fileName.substr(fileName.size() - suffix.size()) == suffix);

        if (matchesPrefix && matchesSuffix && fileName.size() >= prefix.size() + suffix.size()) {
            files.push_back(entry.path().string());
        }
    }

    std::sort(files.begin(), files.end());
    return files;
}

Path normalizePath(const Path& path)
{
    std::filesystem::path p(path);
    std::string normalized = p.make_preferred().string();

    while (normalized.size() > 1 && (normalized.back() == '\\' || normalized.back() == '/')) {
        if (normalized.size() == 3 && normalized[1] == ':') {
            break; // Keep 'C:\'
        }
        normalized.pop_back();
    }

    return normalized;
}

Path joinPath(const Path& left, const std::string& right)
{
    if (left.empty()) return right;
    return (std::filesystem::path(left) / right).string();
}

std::string fileExtension(const std::string& fileName)
{
    return std::filesystem::path(fileName).extension().string();
}

std::uintmax_t directorySize(const Path& path)
{
    std::uintmax_t total = 0;
    std::error_code ec;

    if (!std::filesystem::is_directory(path, ec)) {
        return total;
    }

    auto opts = std::filesystem::directory_options::skip_permission_denied;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(path, opts, ec)) {
        if (entry.is_regular_file(ec)) {
            DWORD high = 0;
            DWORD low = GetCompressedFileSizeW(entry.path().wstring().c_str(), &high);
            if (low != INVALID_FILE_SIZE || GetLastError() == NO_ERROR) {
                total += (static_cast<std::uintmax_t>(high) << 32) | low;
            } else {
                total += entry.file_size(ec);
            }
        }
    }

    return total;
}

} // namespace gsm::system