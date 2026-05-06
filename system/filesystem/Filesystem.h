#pragma once

#include <string>

namespace gsm::system {

using Path = std::string;

bool directoryExists(const Path& path);
Path normalizePath(const Path& path);
Path joinPath(const Path& left, const std::string& right);
std::string fileExtension(const std::string& fileName);

} // namespace gsm::system

