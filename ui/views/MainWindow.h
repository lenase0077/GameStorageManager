#pragma once

#include "core/analyzer/GameAnalysis.h"
#include "core/rules_engine/RecommendationEngine.h"
#include "core/scanner/GameEntry.h"
#include "ui/controllers/AnalysisController.h"

#include <QFutureWatcher>
#include <QLabel>
#include <QMainWindow>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <vector>

namespace gsm::ui {

class MainWindow : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void buildLayout();
    void applyTheme();
    void chooseFolder();
    void startAnalysis(const QString& folderPath);
    void startSteamScan();
    void finishAnalysis();
    void finishSteamScan();
    void showAnalysis(const gsm::core::GameAnalysis& analysis);
    void showSteamGames(const std::vector<gsm::core::GameEntry>& games);
    void setBusy(bool busy);

    QString selectedFolder_;
    AnalysisController analysisController_;
    QFutureWatcher<gsm::core::GameAnalysis> analysisWatcher_;
    QFutureWatcher<std::vector<gsm::core::GameEntry>> steamScanWatcher_;

    QLabel* statusLabel_ = nullptr;
    QLabel* totalSavedLabel_ = nullptr;
    QLabel* selectedFolderLabel_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QPushButton* selectFolderButton_ = nullptr;
    QPushButton* scanSteamButton_ = nullptr;
    QPushButton* analyzeButton_ = nullptr;
    QPushButton* optimizeButton_ = nullptr;
    QPushButton* restoreButton_ = nullptr;
    QTableWidget* gamesTable_ = nullptr;
};

} // namespace gsm::ui
