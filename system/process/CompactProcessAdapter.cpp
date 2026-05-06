#include "system/process/CompactProcessAdapter.h"

#include <array>
#include <cstdio>
#include <sstream>

namespace gsm::system {
namespace {

std::string quoteForDisplay(const std::string& value)
{
    if (value.find_first_of(" \t\"") == std::string::npos) {
        return value;
    }

    std::string quoted = "\"";
    for (char character : value) {
        if (character == '"') {
            quoted += "\\\"";
        } else {
            quoted += character;
        }
    }
    quoted += "\"";
    return quoted;
}

std::string algorithmArgument(gsm::core::CompressionAlgorithm algorithm)
{
    return "/exe:" + gsm::core::toString(algorithm);
}

} // namespace

CompactCommand CompactProcessAdapter::buildCompressCommand(
    const gsm::system::Path& targetPath,
    gsm::core::CompressionAlgorithm algorithm) const
{
    CompactCommand command;
    command.arguments = {
        "/c",
        "/s",
        "/a",
        "/i",
        algorithmArgument(algorithm),
        gsm::system::normalizePath(targetPath)
    };
    return command;
}

CompactCommand CompactProcessAdapter::buildRestoreCommand(const gsm::system::Path& targetPath) const
{
    CompactCommand command;
    command.arguments = {
        "/u",
        "/s",
        gsm::system::normalizePath(targetPath)
    };
    return command;
}

ProcessResult CompactProcessAdapter::run(const CompactCommand& command) const
{
    ProcessResult result;

    const std::string displayCommand = toDisplayString(command) + " 2>&1";
    std::array<char, 256> buffer{};

    FILE* pipe = _popen(displayCommand.c_str(), "r");
    if (pipe == nullptr) {
        result.output = "Failed to start compact.exe.";
        return result;
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result.output += buffer.data();
    }

    result.exitCode = _pclose(pipe);
    return result;
}

std::string toDisplayString(const CompactCommand& command)
{
    std::ostringstream stream;
    stream << quoteForDisplay(command.executable);

    for (const std::string& argument : command.arguments) {
        stream << ' ' << quoteForDisplay(argument);
    }

    return stream.str();
}

} // namespace gsm::system
