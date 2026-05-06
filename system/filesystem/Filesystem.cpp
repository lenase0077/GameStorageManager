#include "system/filesystem/Filesystem.h"

#include <algorithm>
#include <windows.h>

namespace gsm::system {

bool directoryExists(const Path& path)
{
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
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

} // namespace gsm::system

