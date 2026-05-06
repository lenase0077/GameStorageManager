#pragma once

#include "core/compressor/CompressionTask.h"
#include "core/safety/SafetyMetadata.h"

#include <QFuture>
#include <QString>

namespace gsm::ui {

class CompressionController {
public:
    QFuture<gsm::core::CompressionResult> compress(
        const gsm::core::GameAnalysis& analysis,
        const gsm::core::CompressionRecommendation& recommendation) const;

    QFuture<gsm::core::CompressionResult> restore(
        const gsm::core::SafetyMetadata& metadata) const;
};

} // namespace gsm::ui
