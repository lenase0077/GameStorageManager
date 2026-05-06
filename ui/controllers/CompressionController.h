#pragma once

#include "core/compressor/CompressionTask.h"
#include "core/safety/SafetyMetadata.h"

#include <QFuture>
#include <QString>
#include <functional>

namespace gsm::ui {

class CompressionController {
public:
    QFuture<gsm::core::CompressionResult> compress(
        const gsm::core::GameAnalysis& analysis,
        const gsm::core::CompressionRecommendation& recommendation,
        std::function<void(size_t)> onProgress = nullptr) const;

    QFuture<gsm::core::CompressionResult> restore(
        const gsm::core::SafetyMetadata& metadata,
        std::function<void(size_t)> onProgress = nullptr) const;
};

} // namespace gsm::ui
