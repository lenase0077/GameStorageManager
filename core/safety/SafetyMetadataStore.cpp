#include "core/safety/SafetyMetadataStore.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <windows.h>

namespace gsm::core {
namespace {

std::string nowUtcIso8601()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utcTime{};
    gmtime_s(&utcTime, &time);

    std::ostringstream stream;
    stream << std::put_time(&utcTime, "%Y-%m-%dT%H:%M:%SZ");
    return stream.str();
}

std::string escapeValue(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size());

    for (char character : value) {
        switch (character) {
        case '\\':
            escaped += "\\\\";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '|':
            escaped += "\\p";
            break;
        default:
            escaped += character;
            break;
        }
    }

    return escaped;
}

std::string unescapeValue(const std::string& value)
{
    std::string unescaped;
    unescaped.reserve(value.size());

    for (std::size_t index = 0; index < value.size(); ++index) {
        const char character = value[index];
        if (character != '\\' || index + 1 >= value.size()) {
            unescaped += character;
            continue;
        }

        const char next = value[++index];
        switch (next) {
        case 'n':
            unescaped += '\n';
            break;
        case 'r':
            unescaped += '\r';
            break;
        case 'p':
            unescaped += '|';
            break;
        case '\\':
            unescaped += '\\';
            break;
        default:
            unescaped += next;
            break;
        }
    }

    return unescaped;
}

std::vector<std::string> split(const std::string& value, char delimiter)
{
    std::vector<std::string> result;
    std::string current;
    std::istringstream stream(value);

    while (std::getline(stream, current, delimiter)) {
        result.push_back(current);
    }

    if (!value.empty() && value.back() == delimiter) {
        result.emplace_back();
    }

    return result;
}

std::string joinEscapedNotes(const std::vector<std::string>& notes)
{
    std::ostringstream stream;
    for (std::size_t index = 0; index < notes.size(); ++index) {
        if (index > 0) {
            stream << '|';
        }
        stream << escapeValue(notes[index]);
    }
    return stream.str();
}

std::vector<std::string> parseNotes(const std::string& notesText)
{
    std::vector<std::string> notes;
    if (notesText.empty()) {
        return notes;
    }

    for (const std::string& item : split(notesText, '|')) {
        notes.push_back(unescapeValue(item));
    }

    return notes;
}

std::optional<std::uintmax_t> parseUintMax(const std::string& value)
{
    try {
        return static_cast<std::uintmax_t>(std::stoull(value));
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::uint64_t> parseUint64(const std::string& value)
{
    try {
        return static_cast<std::uint64_t>(std::stoull(value));
    } catch (...) {
        return std::nullopt;
    }
}

std::string fallbackNameFromPath(const std::string& path)
{
    const std::string normalized = gsm::system::normalizePath(path);
    const std::size_t separator = normalized.find_last_of("\\/");
    if (separator == std::string::npos) {
        return normalized;
    }
    return normalized.substr(separator + 1);
}

std::string toLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

} // namespace

std::string toString(SafetyOperationState state)
{
    switch (state) {
    case SafetyOperationState::Planned:
        return "Planned";
    case SafetyOperationState::Running:
        return "Running";
    case SafetyOperationState::Completed:
        return "Completed";
    case SafetyOperationState::Failed:
        return "Failed";
    case SafetyOperationState::Restored:
        return "Restored";
    }

    return "Unknown";
}

std::optional<SafetyOperationState> parseSafetyOperationState(const std::string& value)
{
    const std::string lower = toLower(value);

    if (lower == "planned") {
        return SafetyOperationState::Planned;
    }
    if (lower == "running") {
        return SafetyOperationState::Running;
    }
    if (lower == "completed") {
        return SafetyOperationState::Completed;
    }
    if (lower == "failed") {
        return SafetyOperationState::Failed;
    }
    if (lower == "restored") {
        return SafetyOperationState::Restored;
    }

    return std::nullopt;
}

SafetyMetadataStore::SafetyMetadataStore(gsm::system::Path storageRoot)
    : storageRoot_(gsm::system::normalizePath(storageRoot))
{
}

SafetyMetadata SafetyMetadataStore::createPlannedMetadata(
    const GameAnalysis& analysis,
    const CompressionRecommendation& recommendation,
    const std::string& gameName,
    const std::string& source) const
{
    SafetyMetadata metadata;
    metadata.id = makeStableId(analysis.rootPath);
    metadata.gameName = gameName.empty() ? fallbackNameFromPath(analysis.rootPath) : gameName;
    metadata.rootPath = gsm::system::normalizePath(analysis.rootPath);
    metadata.source = source.empty() ? "manual" : source;
    metadata.sizeBeforeBytes = analysis.totalBytes;
    metadata.fileCountBefore = analysis.fileCount;
    metadata.algorithm = recommendation.algorithm;
    metadata.state = SafetyOperationState::Planned;
    metadata.createdAtUtc = nowUtcIso8601();
    metadata.updatedAtUtc = metadata.createdAtUtc;

    for (const std::string& reason : recommendation.reasons) {
        metadata.notes.push_back("recommendation:" + reason);
    }

    return metadata;
}

bool SafetyMetadataStore::save(const SafetyMetadata& metadata) const
{
    if (metadata.id.empty() || !gsm::system::ensureDirectoryExists(storageRoot_)) {
        return false;
    }

    std::ofstream file(metadataPathForId(metadata.id), std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }

    file << serializeSafetyMetadata(metadata);
    return file.good();
}

std::optional<SafetyMetadata> SafetyMetadataStore::loadById(const std::string& id) const
{
    std::ifstream file(metadataPathForId(id), std::ios::binary);
    if (!file) {
        return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return deserializeSafetyMetadata(buffer.str());
}

std::vector<SafetyMetadata> SafetyMetadataStore::loadAll() const
{
    std::vector<SafetyMetadata> records;
    const std::string searchPath = gsm::system::joinPath(storageRoot_, "*.gsmmeta");

    WIN32_FIND_DATAA findData;
    HANDLE findHandle = FindFirstFileA(searchPath.c_str(), &findData);
    if (findHandle == INVALID_HANDLE_VALUE) {
        return records;
    }

    do {
        const std::string fileName = findData.cFileName;
        const auto metadata = loadById(fileName.substr(0, fileName.find_last_of('.')));
        if (metadata.has_value()) {
            records.push_back(*metadata);
        }
    } while (FindNextFileA(findHandle, &findData) != 0);

    FindClose(findHandle);

    std::sort(records.begin(), records.end(), [](const SafetyMetadata& left, const SafetyMetadata& right) {
        return left.updatedAtUtc > right.updatedAtUtc;
    });

    return records;
}

gsm::system::Path SafetyMetadataStore::storageRoot() const
{
    return storageRoot_;
}

gsm::system::Path SafetyMetadataStore::metadataPathForId(const std::string& id) const
{
    return gsm::system::joinPath(storageRoot_, id + ".gsmmeta");
}

std::string SafetyMetadataStore::makeStableId(const std::string& rootPath)
{
    const std::string normalized = toLower(gsm::system::normalizePath(rootPath));
    std::uint64_t hash = 1469598103934665603ULL;

    for (unsigned char character : normalized) {
        hash ^= character;
        hash *= 1099511628211ULL;
    }

    std::ostringstream stream;
    stream << std::hex << hash;
    return stream.str();
}

std::string serializeSafetyMetadata(const SafetyMetadata& metadata)
{
    std::ostringstream stream;
    stream << "schemaVersion=" << metadata.schemaVersion << '\n';
    stream << "id=" << escapeValue(metadata.id) << '\n';
    stream << "gameName=" << escapeValue(metadata.gameName) << '\n';
    stream << "rootPath=" << escapeValue(metadata.rootPath) << '\n';
    stream << "source=" << escapeValue(metadata.source) << '\n';
    stream << "sizeBeforeBytes=" << metadata.sizeBeforeBytes << '\n';
    stream << "sizeAfterBytes=" << metadata.sizeAfterBytes << '\n';
    stream << "fileCountBefore=" << metadata.fileCountBefore << '\n';
    stream << "algorithm=" << (metadata.algorithm.has_value() ? toString(*metadata.algorithm) : "") << '\n';
    stream << "state=" << toString(metadata.state) << '\n';
    stream << "createdAtUtc=" << escapeValue(metadata.createdAtUtc) << '\n';
    stream << "updatedAtUtc=" << escapeValue(metadata.updatedAtUtc) << '\n';
    stream << "notes=" << joinEscapedNotes(metadata.notes) << '\n';
    return stream.str();
}

std::optional<SafetyMetadata> deserializeSafetyMetadata(const std::string& text)
{
    std::map<std::string, std::string> values;
    std::istringstream stream(text);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.empty()) {
            continue;
        }

        const std::size_t separator = line.find('=');
        if (separator == std::string::npos) {
            return std::nullopt;
        }

        values[line.substr(0, separator)] = line.substr(separator + 1);
    }

    SafetyMetadata metadata;

    if (values.find("schemaVersion") == values.end() || values["schemaVersion"] != "1") {
        return std::nullopt;
    }

    metadata.schemaVersion = 1;
    metadata.id = unescapeValue(values["id"]);
    metadata.gameName = unescapeValue(values["gameName"]);
    metadata.rootPath = unescapeValue(values["rootPath"]);
    metadata.source = unescapeValue(values["source"]);

    const auto sizeBefore = parseUintMax(values["sizeBeforeBytes"]);
    const auto sizeAfter = parseUintMax(values["sizeAfterBytes"]);
    const auto fileCount = parseUint64(values["fileCountBefore"]);
    const auto state = parseSafetyOperationState(values["state"]);

    if (!sizeBefore.has_value() || !sizeAfter.has_value() || !fileCount.has_value() || !state.has_value()) {
        return std::nullopt;
    }

    metadata.sizeBeforeBytes = *sizeBefore;
    metadata.sizeAfterBytes = *sizeAfter;
    metadata.fileCountBefore = *fileCount;
    metadata.state = *state;
    metadata.createdAtUtc = unescapeValue(values["createdAtUtc"]);
    metadata.updatedAtUtc = unescapeValue(values["updatedAtUtc"]);
    metadata.notes = parseNotes(values["notes"]);

    if (!values["algorithm"].empty()) {
        metadata.algorithm = parseCompressionAlgorithm(values["algorithm"]);
        if (!metadata.algorithm.has_value()) {
            return std::nullopt;
        }
    }

    return metadata;
}

} // namespace gsm::core

