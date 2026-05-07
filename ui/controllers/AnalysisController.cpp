#include "ui/controllers/AnalysisController.h"

#include "core/analyzer/GameAnalyzer.h"
#include "core/scanner/SteamScanner.h"

#include <QtConcurrent/QtConcurrentRun>

namespace gsm::ui {

QFuture<gsm::core::GameAnalysis> AnalysisController::analyzeFolder(const QString& folderPath, const QString& gameName)
{
    const std::string path = folderPath.toStdString();
    const std::string name = gameName.toStdString();

    cancelFlag_->store(false);
    auto flag = cancelFlag_;
    return QtConcurrent::run([path, name, flag]() {
        gsm::core::GameAnalyzer analyzer;
        return analyzer.analyze(path, name, flag.get());
    });
}

QFuture<std::vector<gsm::core::GameEntry>> AnalysisController::scanSteamGames()
{
    return QtConcurrent::run([]() {
        gsm::core::SteamScanner scanner;
        return scanner.scanInstalledGames();
    });
}

void AnalysisController::cancel()
{
    cancelFlag_->store(true);
}

} // namespace gsm::ui
