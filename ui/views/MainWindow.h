#pragma once

#include "core/analyzer/GameAnalysis.h"
#include "core/compressor/CompressionTask.h"
#include "core/rules_engine/RecommendationEngine.h"
#include "core/safety/SafetyMetadata.h"
#include "core/scanner/GameEntry.h"
#include "ui/controllers/AnalysisController.h"
#include "ui/controllers/CompressionController.h"

#include <QFutureWatcher>
#include <QLabel>
#include <QMainWindow>
#include <QProgressBar>
#include <QPushButton>
#include <QComboBox>
#include <QTableWidget>
#include <optional>
#include <map>
#include <vector>

namespace gsm::ui {

class MainWindow : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void buildLayout();
    void applyTheme();
    void chooseFolder();
    void startAnalysis(const QString& folderPath, const QString& gameName = {});
    void startSteamScan();
    void finishAnalysis();
    void finishSteamScan();
    void onAnalyzeSelected();
    void onOptimize();
    void finishCompression();
    void onRestore();
    void finishRestore();
    void showAnalysis(const gsm::core::GameAnalysis& analysis);
    void refreshTableView();
    void updateGameRow(int row, const gsm::core::GameAnalysis& analysis);
    void setBusy(bool busy);
    void updateActiveState(const gsm::core::GameAnalysis& analysis,
                           const gsm::core::CompressionRecommendation& recommendation);
    void updateRowStatus(int row, const QString& status);
    void applyStoredMetadata();
    void updateActiveRowFromMetadata(const std::string& normalizedPath);
    void onProfileChanged(int index);
    void onRemoveGame();
    bool tryGetRowAnalysis(int row, gsm::core::GameAnalysis& outAnalysis,
                           gsm::core::CompressionRecommendation& outRecommendation) const;

    void loadLibrary();
    void saveLibrary();

    static QString extractDriveLetter(const QString& path);
    static QString driveSpaceInfo(const QString& driveLetter);

    QString selectedFolder_;
    int analyzingRow_ = -1;
    int activeRow_ = -1;
    std::optional<gsm::core::GameAnalysis> activeAnalysis_;
    std::optional<gsm::core::CompressionRecommendation> activeRecommendation_;

    std::vector<gsm::core::GameEntry> libraryGames_;

    AnalysisController analysisController_;
    CompressionController compressionController_;
    QFutureWatcher<gsm::core::GameAnalysis> analysisWatcher_;
    QFutureWatcher<std::vector<gsm::core::GameEntry>> steamScanWatcher_;
    QFutureWatcher<gsm::core::CompressionResult> compressWatcher_;
    QFutureWatcher<gsm::core::CompressionResult> restoreWatcher_;

    QLabel* statusLabel_ = nullptr;
    QLabel* totalSavedLabel_ = nullptr;
    QLabel* gamesCountLabel_ = nullptr;
    QLabel* spaceSavedLabel_ = nullptr;
    QLabel* ratioLabel_ = nullptr;
    QLabel* selectedFolderLabel_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QPushButton* selectFolderButton_ = nullptr;
    QPushButton* scanSteamButton_ = nullptr;
    QPushButton* analyzeButton_ = nullptr;
    QPushButton* analyzeSelectedButton_ = nullptr;
    QPushButton* optimizeButton_ = nullptr;
    QPushButton* restoreButton_ = nullptr;
    QPushButton* removeButton_ = nullptr;
    QComboBox* profileCombo_ = nullptr;
    QTableWidget* gamesTable_ = nullptr;

    gsm::core::CompressionAlgorithm currentAlgorithm_ = gsm::core::CompressionAlgorithm::Xpress8k;

    std::map<int, gsm::core::GameAnalysis> rowAnalyses_;
    std::map<int, gsm::core::CompressionRecommendation> rowRecommendations_;
};

} // namespace gsm::ui
