#include "ui/controllers/AnalysisController.h"

#include "core/analyzer/GameAnalyzer.h"
#include "core/scanner/SteamScanner.h"

#include <QtConcurrent/QtConcurrentRun>

namespace gsm::ui {

QFuture<gsm::core::GameAnalysis> AnalysisController::analyzeFolder(const QString& folderPath, const QString& gameName) const
{
    const std::string path = folderPath.toStdString();
    const std::string name = gameName.toStdString();

    return QtConcurrent::run([path, name]() {
        gsm::core::GameAnalyzer analyzer;
        return analyzer.analyze(path, name);
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
