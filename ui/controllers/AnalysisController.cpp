#include "ui/controllers/AnalysisController.h"

#include "core/analyzer/GameAnalyzer.h"
#include "core/scanner/SteamScanner.h"

#include <QtConcurrent/QtConcurrentRun>

namespace gsm::ui {

QFuture<gsm::core::GameAnalysis> AnalysisController::analyzeFolder(const QString& folderPath) const
{
    const std::string path = folderPath.toStdString();

    return QtConcurrent::run([path]() {
        gsm::core::GameAnalyzer analyzer;
        return analyzer.analyze(path);
    });
}

QFuture<std::vector<gsm::core::GameEntry>> AnalysisController::scanSteamGames() const
{
    return QtConcurrent::run([]() {
        gsm::core::SteamScanner scanner;
        return scanner.scanInstalledGames();
    });
}

} // namespace gsm::ui
