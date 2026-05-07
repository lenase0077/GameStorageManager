#pragma once

#include "core/compressor/CompressionTask.h"
#include "core/safety/SafetyMetadata.h"

#include <QFuture>
#include <QString>
#include <functional>

#include <atomic>
#include <memory>

namespace gsm::ui {

class CompressionController {
public:
    QFuture<gsm::core::CompressionResult> compress(
        const gsm::core::GameAnalysis& analysis,
        const gsm::core::CompressionRecommendation& recommendation,
        std::function<void(size_t)> onProgress = nullptr);

    QFuture<gsm::core::CompressionResult> restore(
        const gsm::core::SafetyMetadata& metadata,
        std::function<void(size_t)> onProgress = nullptr);

    void cancel();

private:
    std::shared_ptr<std::atomic<bool>> cancelFlag_ = std::make_shared<std::atomic<bool>>(false);
};

} // namespace gsm::ui
