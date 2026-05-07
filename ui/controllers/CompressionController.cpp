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
    const gsm::core::CompressionRecommendation& recommendation,
    std::function<void(size_t)> onProgress)
{
    cancelFlag_->store(false);
    auto flag = cancelFlag_;
    return QtConcurrent::run([analysis, recommendation, onProgress, flag]() {
        gsm::core::Compressor compressor;
        gsm::core::SafetyMetadataStore store(appStorageRoot());
        return compressor.compress(analysis, recommendation, store, onProgress, flag.get());
    });
}

QFuture<gsm::core::CompressionResult> CompressionController::restore(
    const gsm::core::SafetyMetadata& metadata,
    std::function<void(size_t)> onProgress)
{
    const std::string targetPath = metadata.rootPath;
    cancelFlag_->store(false);
    auto flag = cancelFlag_;
    return QtConcurrent::run([metadata, targetPath, onProgress, flag]() {
        gsm::core::Compressor compressor;
        gsm::core::SafetyMetadataStore store(appStorageRoot());
        return compressor.restore(metadata, store, targetPath, onProgress, flag.get());
    });
}

void CompressionController::cancel()
{
    cancelFlag_->store(true);
}

} // namespace gsm::ui
