#include "system/process/CompactProcessAdapter.h"

#include <array>
#include <cstdio>
#include <cstdlib>
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

namespace {

bool parseSummaryNumber(const std::string& line, const std::string& pattern, std::uint64_t& outValue)
{
    const auto pos = line.find(pattern);
    if (pos == std::string::npos) return false;

    // find digits at start of line (trimming whitespace)
    std::size_t numStart = 0;
    while (numStart < line.size() && (line[numStart] == ' ' || line[numStart] == '\t')) {
        ++numStart;
    }

    std::size_t numEnd = numStart;
    while (numEnd < pos && line[numEnd] >= '0' && line[numEnd] <= '9') {
        ++numEnd;
    }

    if (numEnd > numStart) {
        try {
            outValue = std::stoull(line.substr(numStart, numEnd - numStart));
            return true;
        } catch (...) {
        }
    }

    return false;
}

} // namespace

CompactOutputMetrics CompactProcessAdapter::parseCompressOutput(const std::string& output)
{
    CompactOutputMetrics metrics;

    std::istringstream stream(output);
    std::string line;
    std::uintmax_t okBefore = 0;
    std::uintmax_t okAfter = 0;

    while (std::getline(stream, line)) {
        // English: "was 1234 bytes, now 567 bytes"
        auto wasPos = line.find("was ");
        auto nowTag = std::string(" bytes, now ");
        if (wasPos != std::string::npos) {
            auto nowPos = line.find(nowTag, wasPos);
            if (nowPos != std::string::npos) {
                auto endPos = line.find(" bytes", nowPos + nowTag.size());
                if (endPos != std::string::npos) {
                    try {
                        okBefore += std::stoull(line.substr(wasPos + 4, nowPos - wasPos - 4));
                        okAfter += std::stoull(line.substr(nowPos + nowTag.size(), endPos - nowPos - nowTag.size()));
                        ++metrics.filesProcessed;
                    } catch (...) {}
                }
            }
        }

        // Spanish: "tenía 1234 bytes, ahora 567 bytes"
        auto teniaPos = line.find("ten");
        if (teniaPos != std::string::npos && line.find(" bytes, ahor", teniaPos) != std::string::npos) {
            auto ahoraPos = line.find(" bytes, ahor", teniaPos);
            auto numStart = teniaPos + 5; // skip "tenía "
            while (numStart < ahoraPos && line[numStart] == ' ') ++numStart;
            auto numEnd = line.find(" bytes,", numStart);
            if (numEnd != std::string::npos && numEnd < ahoraPos) {
                auto ahoraNumStart = ahoraPos + 14; // skip " bytes, ahora "
                while (ahoraNumStart < line.size() && line[ahoraNumStart] == ' ') ++ahoraNumStart;
                auto ahoraNumEnd = line.find(" bytes", ahoraNumStart);
                if (ahoraNumEnd != std::string::npos) {
                    try {
                        okBefore += std::stoull(line.substr(numStart, numEnd - numStart));
                        okAfter += std::stoull(line.substr(ahoraNumStart, ahoraNumEnd - ahoraNumStart));
                        ++metrics.filesProcessed;
                    } catch (...) {}
                }
            }
        }

        // Count-already: EN "are already compressed" / ES "están comprimidos"
        parseSummaryNumber(line, " are already compressed", metrics.filesAlreadyCompressed);
        parseSummaryNumber(line, "están comprimidos", metrics.filesAlreadyCompressed);

        // Count-compressed: EN "were compressed" / ES "se comprimieron" / "fueron comprimidos"
        if (!parseSummaryNumber(line, " were compressed", metrics.filesCompressed)) {
            if (!parseSummaryNumber(line, "se comprimieron", metrics.filesCompressed)) {
                parseSummaryNumber(line, "fueron comprimidos", metrics.filesCompressed);
            }
        }
    }

    if (okBefore > 0) {
        metrics.parsed = true;
        metrics.bytesBeforeFromOutput = okBefore;
        metrics.bytesAfterFromOutput = okAfter;
    }

    return metrics;
}

} // namespace gsm::system
