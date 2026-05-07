#pragma once

#include "core/analyzer/GameAnalysis.h"
#include "core/scanner/GameEntry.h"

#include <QFuture>
#include <QString>
#include <vector>

#include <atomic>
#include <memory>

namespace gsm::ui {

class AnalysisController {
public:
    QFuture<gsm::core::GameAnalysis> analyzeFolder(const QString& folderPath, const QString& gameName = {});
    QFuture<std::vector<gsm::core::GameEntry>> scanSteamGames();
    
    void cancel();

private:
    std::shared_ptr<std::atomic<bool>> cancelFlag_ = std::make_shared<std::atomic<bool>>(false);
};

} // namespace gsm::ui
