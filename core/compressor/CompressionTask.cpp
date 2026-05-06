#include "core/compressor/CompressionTask.h"

#include "system/filesystem/Filesystem.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace gsm::core {
namespace {

std::string utcNow()
{
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::ostringstream stream;
    stream << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%SZ");
    return stream.str();
}

} // namespace

CompressionResult Compressor::compress(
    const GameAnalysis& analysis,
    const CompressionRecommendation& recommendation,
    SafetyMetadataStore& metadataStore,
    std::function<void(size_t)> onProgress) const
{
    CompressionResult result;
    result.bytesBefore = analysis.totalBytes;

    if (!analysis.isValid) {
        result.errorMessage = "Cannot compress: analysis is invalid.";
        return result;
    }

    if (recommendation.action != RecommendationAction::Compress) {
        result.errorMessage = "Cannot compress: game is marked as Skip.";
        return result;
    }

    if (!recommendation.algorithm.has_value()) {
        result.errorMessage = "Cannot compress: no algorithm selected.";
        return result;
    }

    if (!gsm::system::directoryExists(analysis.rootPath)) {
        result.errorMessage = "Target path no longer exists.";
        return result;
    }

    const auto algorithm = *recommendation.algorithm;

    SafetyMetadata metadata = metadataStore.createPlannedMetadata(
        analysis,
        recommendation,
        analysis.gameName,
        "manual"
    );
    metadata.state = SafetyOperationState::Running;
    metadata.createdAtUtc = utcNow();
    metadata.updatedAtUtc = metadata.createdAtUtc;

    if (!metadataStore.save(metadata)) {
        result.errorMessage = "Failed to save safety metadata before compression.";
        return result;
    }

    size_t lineCount = 0;
    auto outputCallback = [&lineCount, onProgress](const std::string& chunk) {
        if (!onProgress) return;
        for (char c : chunk) {
            if (c == '\n') {
                ++lineCount;
            }
        }
        onProgress(lineCount);
    };

    const auto command = adapter_.buildCompressCommand(analysis.rootPath, algorithm);
    const auto processResult = adapter_.run(command, outputCallback);

    result.exitCode = processResult.exitCode;
    result.output = processResult.output;
    result.metrics = gsm::system::CompactProcessAdapter::parseCompressOutput(processResult.output);

    if (processResult.exitCode != 0) {
        metadata.state = SafetyOperationState::Failed;
        metadata.notes.push_back("compact.exe exit code: " + std::to_string(processResult.exitCode));
        metadata.updatedAtUtc = utcNow();
        metadataStore.save(metadata);
        result.errorMessage = "compact.exe returned non-zero exit code.";
        return result;
    }

    const auto& metrics = result.metrics;
    if (metrics.parsed && analysis.totalBytes > 0) {
        const std::uintmax_t unchangedBytes = analysis.totalBytes - metrics.bytesBeforeFromOutput;
        metadata.sizeAfterBytes = unchangedBytes + metrics.bytesAfterFromOutput;
    } else {
        metadata.sizeAfterBytes = gsm::system::directorySize(analysis.rootPath);
    }
    metadata.state = SafetyOperationState::Completed;
    metadata.updatedAtUtc = utcNow();

    if (!metadataStore.save(metadata)) {
        result.errorMessage = "Compression succeeded but failed to save metadata.";
        result.success = true;
        result.bytesAfter = metadata.sizeAfterBytes;
        return result;
    }

    result.success = true;
    result.bytesAfter = metadata.sizeAfterBytes;
    return result;
}

CompressionResult Compressor::restore(
    const SafetyMetadata& metadata,
    SafetyMetadataStore& metadataStore,
    const gsm::system::Path& targetPath,
    std::function<void(size_t)> onProgress) const
{
    CompressionResult result;
    result.bytesBefore = metadata.sizeAfterBytes;

    if (!gsm::system::directoryExists(targetPath)) {
        result.errorMessage = "Target path no longer exists.";
        return result;
    }

    size_t lineCount = 0;
    auto outputCallback = [&lineCount, onProgress](const std::string& chunk) {
        if (!onProgress) return;
        for (char c : chunk) {
            if (c == '\n') {
                ++lineCount;
            }
        }
        onProgress(lineCount);
    };

    const auto command = adapter_.buildRestoreCommand(targetPath);
    const auto processResult = adapter_.run(command, outputCallback);

    result.exitCode = processResult.exitCode;
    result.output = processResult.output;

    if (processResult.exitCode != 0) {
        SafetyMetadata updated = metadata;
        updated.state = SafetyOperationState::Failed;
        updated.notes.push_back("Restore failed. compact.exe exit code: " + std::to_string(processResult.exitCode));
        metadataStore.save(updated);
        result.errorMessage = "compact.exe returned non-zero exit code during restore.";
        return result;
    }

    result.bytesAfter = gsm::system::directorySize(targetPath);
    result.success = true;

    SafetyMetadata updated = metadata;
    updated.state = SafetyOperationState::Restored;
    updated.sizeAfterBytes = result.bytesAfter;
    updated.notes.push_back("Restored to original state.");
    metadataStore.save(updated);

    return result;
}

} // namespace gsm::core
