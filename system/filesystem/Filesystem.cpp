#include "system/filesystem/Filesystem.h"

#include <algorithm>
#include <cctype>
#include <windows.h>

namespace gsm::system {

bool directoryExists(const Path& path)
{
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool ensureDirectoryExists(const Path& path)
{
    const Path normalized = normalizePath(path);
    if (normalized.empty() || directoryExists(normalized)) {
        return true;
    }

    const std::size_t separator = normalized.find_last_of("\\/");
    if (separator != std::string::npos) {
        const Path parent = normalized.substr(0, separator);
        if (!parent.empty() && !directoryExists(parent) && !ensureDirectoryExists(parent)) {
            return false;
        }
    }

    if (CreateDirectoryA(normalized.c_str(), nullptr) != 0) {
        return true;
    }

    return GetLastError() == ERROR_ALREADY_EXISTS;
}

bool fileExists(const Path& path)
{
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::vector<Path> listFilesWithPrefixSuffix(const Path& directory, const std::string& prefix, const std::string& suffix)
{
    std::vector<Path> files;
    const Path normalizedDirectory = normalizePath(directory);
    const Path searchPath = joinPath(normalizedDirectory, prefix + "*" + suffix);

    WIN32_FIND_DATAA findData;
    HANDLE findHandle = FindFirstFileA(searchPath.c_str(), &findData);
    if (findHandle == INVALID_HANDLE_VALUE) {
        return files;
    }

    do {
        const bool isDirectory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (!isDirectory) {
            files.push_back(joinPath(normalizedDirectory, findData.cFileName));
        }
    } while (FindNextFileA(findHandle, &findData) != 0);

    FindClose(findHandle);
    std::sort(files.begin(), files.end());
    return files;
}

Path normalizePath(const Path& path)
{
    Path normalized = path;
    std::replace(normalized.begin(), normalized.end(), '/', '\\');

    while (normalized.size() > 1 && (normalized.back() == '\\' || normalized.back() == '/')) {
        if (normalized.size() == 3 && normalized[1] == ':') {
            break;
        }
        normalized.pop_back();
    }

    return normalized;
}

Path joinPath(const Path& left, const std::string& right)
{
    if (left.empty()) {
        return right;
    }

    if (left.back() == '\\' || left.back() == '/') {
        return left + right;
    }

    return left + "\\" + right;
}

std::string fileExtension(const std::string& fileName)
{
    const std::size_t slashPosition = fileName.find_last_of("\\/");
    const std::size_t dotPosition = fileName.find_last_of('.');

    if (dotPosition == std::string::npos) {
        return {};
    }

    if (slashPosition != std::string::npos && dotPosition < slashPosition) {
        return {};
    }

    return fileName.substr(dotPosition);
}

namespace {

void accumulateDirectorySize(const Path& directory, std::uintmax_t& total)
{
    const Path searchPath = joinPath(directory, "*");
    WIN32_FIND_DATAA findData;
    HANDLE findHandle = FindFirstFileA(searchPath.c_str(), &findData);

    if (findHandle == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        const std::string fileName = findData.cFileName;
        if (fileName == "." || fileName == "..") {
            continue;
        }

        const bool isDirectory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const bool isReparsePoint = (findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

        if (isDirectory) {
            if (!isReparsePoint) {
                accumulateDirectorySize(joinPath(directory, fileName), total);
            }
            continue;
        }

        ULARGE_INTEGER size;
        size.HighPart = findData.nFileSizeHigh;
        size.LowPart = findData.nFileSizeLow;
        total += size.QuadPart;
    } while (FindNextFileA(findHandle, &findData) != 0);

    FindClose(findHandle);
}

} // namespace

std::uintmax_t directorySize(const Path& path)
{
    std::uintmax_t total = 0;
    accumulateDirectorySize(normalizePath(path), total);
    return total;
}

} // namespace gsm::system
