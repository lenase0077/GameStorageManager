#include "ui/controllers/CompressionController.h"

#include "core/safety/SafetyMetadataStore.h"
#include "system/filesystem/Filesystem.h"

#include <QtConcurrent/QtConcurrentRun>
#include <cstdlib>

namespace gsm::ui {
namespace {

gsm::system::Path appStorageRoot()
{
    const char* localAppData = std::getenv("LOCALAPPDATA");
    if (localAppData != nullptr) {
        return gsm::system::joinPath(localAppData, "GameStorageManager/metadata");
    }
    return "metadata";
}

} // namespace

QFuture<gsm::core::CompressionResult> CompressionController::compress(
    const gsm::core::GameAnalysis& analysis,
    const gsm::core::CompressionRecommendation& recommendation) const
{
    return QtConcurrent::run([analysis, recommendation]() {
        gsm::core::Compressor compressor;
        gsm::core::SafetyMetadataStore store(appStorageRoot());
        return compressor.compress(analysis, recommendation, store);
    });
}

QFuture<gsm::core::CompressionResult> CompressionController::restore(
    const gsm::core::SafetyMetadata& metadata) const
{
    const std::string targetPath = metadata.rootPath;
    return QtConcurrent::run([metadata, targetPath]() {
        gsm::core::Compressor compressor;
        gsm::core::SafetyMetadataStore store(appStorageRoot());
        return compressor.restore(metadata, store, targetPath);
    });
}

} // namespace gsm::ui
