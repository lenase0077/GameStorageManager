#pragma once

#include "core/analyzer/GameAnalysis.h"
#include "core/rules_engine/RecommendationEngine.h"
#include "ui/controllers/AnalysisController.h"

#include <QFutureWatcher>
#include <QLabel>
#include <QMainWindow>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>

namespace gsm::ui {

class MainWindow : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void buildLayout();
    void applyTheme();
    void chooseFolder();
    void startAnalysis(const QString& folderPath);
    void finishAnalysis();
    void showAnalysis(const gsm::core::GameAnalysis& analysis);
    void setBusy(bool busy);

    QString selectedFolder_;
    AnalysisController analysisController_;
    QFutureWatcher<gsm::core::GameAnalysis> analysisWatcher_;

    QLabel* statusLabel_ = nullptr;
    QLabel* totalSavedLabel_ = nullptr;
    QLabel* selectedFolderLabel_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QPushButton* selectFolderButton_ = nullptr;
    QPushButton* analyzeButton_ = nullptr;
    QPushButton* optimizeButton_ = nullptr;
    QPushButton* restoreButton_ = nullptr;
    QTableWidget* gamesTable_ = nullptr;
};

} // namespace gsm::ui

