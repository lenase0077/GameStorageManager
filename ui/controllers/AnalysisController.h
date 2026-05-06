#pragma once

#include "core/analyzer/GameAnalysis.h"
#include "core/scanner/GameEntry.h"

#include <QFuture>
#include <QString>
#include <vector>

namespace gsm::ui {

class AnalysisController {
public:
    QFuture<gsm::core::GameAnalysis> analyzeFolder(const QString& folderPath) const;
    QFuture<std::vector<gsm::core::GameEntry>> scanSteamGames() const;
};

} // namespace gsm::ui
