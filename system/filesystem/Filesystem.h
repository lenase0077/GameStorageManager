#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gsm::system {

using Path = std::string;

bool directoryExists(const Path& path);
bool ensureDirectoryExists(const Path& path);
bool fileExists(const Path& path);
std::vector<Path> listFilesWithPrefixSuffix(const Path& directory, const std::string& prefix, const std::string& suffix);
Path normalizePath(const Path& path);
Path joinPath(const Path& left, const std::string& right);
std::string fileExtension(const std::string& fileName);
std::uintmax_t directorySize(const Path& path);

} // namespace gsm::system
