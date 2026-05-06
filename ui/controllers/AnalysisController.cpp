#include "ui/controllers/AnalysisController.h"

#include "core/analyzer/GameAnalyzer.h"

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

} // namespace gsm::ui

