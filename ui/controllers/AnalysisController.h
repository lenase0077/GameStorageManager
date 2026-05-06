#pragma once

#include "core/analyzer/GameAnalysis.h"

#include <QFuture>
#include <QString>

namespace gsm::ui {

class AnalysisController {
public:
    QFuture<gsm::core::GameAnalysis> analyzeFolder(const QString& folderPath) const;
};

} // namespace gsm::ui

