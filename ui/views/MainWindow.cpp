#include "ui/views/MainWindow.h"

#include <QFileDialog>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

namespace gsm::ui {
namespace {

QString formatBytes(std::uintmax_t bytes)
{
    constexpr double kib = 1024.0;
    constexpr double mib = kib * 1024.0;
    constexpr double gib = mib * 1024.0;

    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2);

    if (bytes >= static_cast<std::uintmax_t>(gib)) {
        stream << static_cast<double>(bytes) / gib << " GB";
    } else if (bytes >= static_cast<std::uintmax_t>(mib)) {
        stream << static_cast<double>(bytes) / mib << " MB";
    } else if (bytes >= static_cast<std::uintmax_t>(kib)) {
        stream << static_cast<double>(bytes) / kib << " KB";
    } else {
        stream << bytes << " B";
    }

    return QString::fromStdString(stream.str());
}

QString baseNameFromPath(const QString& path)
{
    const QString normalized = QString(path).replace('\\', '/');
    const QString trimmed = normalized.endsWith('/') ? normalized.left(normalized.size() - 1) : normalized;
    const int slashIndex = trimmed.lastIndexOf('/');
    return slashIndex >= 0 ? trimmed.mid(slashIndex + 1) : trimmed;
}

QString recommendationText(const gsm::core::CompressionRecommendation& recommendation)
{
    if (recommendation.action == gsm::core::RecommendationAction::Skip) {
        return "Skip";
    }

    if (!recommendation.algorithm.has_value()) {
        return "Analyze";
    }

    return QString::fromStdString(gsm::core::toString(*recommendation.algorithm));
}

QString reasonText(const gsm::core::CompressionRecommendation& recommendation)
{
    QStringList reasons;
    for (const std::string& reason : recommendation.reasons) {
        reasons.append(QString::fromStdString(reason));
    }
    return reasons.join(", ");
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    buildLayout();
    applyTheme();

    connect(selectFolderButton_, &QPushButton::clicked, this, [this]() {
        chooseFolder();
    });

    connect(scanSteamButton_, &QPushButton::clicked, this, [this]() {
        startSteamScan();
    });

    connect(analyzeButton_, &QPushButton::clicked, this, [this]() {
        if (!selectedFolder_.isEmpty()) {
            startAnalysis(selectedFolder_);
        }
    });

    connect(&analysisWatcher_, &QFutureWatcher<gsm::core::GameAnalysis>::finished, this, [this]() {
        finishAnalysis();
    });

    connect(&steamScanWatcher_, &QFutureWatcher<std::vector<gsm::core::GameEntry>>::finished, this, [this]() {
        finishSteamScan();
    });

    setBusy(false);
}

void MainWindow::buildLayout()
{
    auto* centralWidget = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(centralWidget);
    rootLayout->setContentsMargins(24, 20, 24, 20);
    rootLayout->setSpacing(14);

    auto* headerLayout = new QHBoxLayout();
    auto* titleBlock = new QVBoxLayout();

    auto* titleLabel = new QLabel("Game Storage Manager", centralWidget);
    titleLabel->setObjectName("titleLabel");
    statusLabel_ = new QLabel("Ready", centralWidget);
    statusLabel_->setObjectName("statusLabel");

    titleBlock->addWidget(titleLabel);
    titleBlock->addWidget(statusLabel_);
    headerLayout->addLayout(titleBlock, 1);

    totalSavedLabel_ = new QLabel("Saved: 0 B", centralWidget);
    totalSavedLabel_->setObjectName("metricLabel");
    headerLayout->addWidget(totalSavedLabel_, 0);
    rootLayout->addLayout(headerLayout);

    auto* actionFrame = new QFrame(centralWidget);
    actionFrame->setObjectName("toolbarFrame");
    auto* actionLayout = new QHBoxLayout(actionFrame);
    actionLayout->setContentsMargins(14, 12, 14, 12);
    actionLayout->setSpacing(10);

    selectFolderButton_ = new QPushButton("Select Folder", actionFrame);
    scanSteamButton_ = new QPushButton("Scan Steam", actionFrame);
    analyzeButton_ = new QPushButton("Analyze", actionFrame);
    optimizeButton_ = new QPushButton("Optimize", actionFrame);
    restoreButton_ = new QPushButton("Restore", actionFrame);
    selectedFolderLabel_ = new QLabel("No folder selected", actionFrame);
    selectedFolderLabel_->setObjectName("pathLabel");

    optimizeButton_->setEnabled(false);
    restoreButton_->setEnabled(false);

    actionLayout->addWidget(selectFolderButton_);
    actionLayout->addWidget(scanSteamButton_);
    actionLayout->addWidget(analyzeButton_);
    actionLayout->addWidget(optimizeButton_);
    actionLayout->addWidget(restoreButton_);
    actionLayout->addWidget(selectedFolderLabel_, 1);
    rootLayout->addWidget(actionFrame);

    gamesTable_ = new QTableWidget(0, 8, centralWidget);
    gamesTable_->setHorizontalHeaderLabels({
        "Game",
        "Path",
        "Size",
        "Files",
        "Compressed assets",
        "Recommendation",
        "Risk",
        "Status"
    });
    gamesTable_->verticalHeader()->setVisible(false);
    gamesTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    gamesTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    gamesTable_->setAlternatingRowColors(true);
    gamesTable_->horizontalHeader()->setStretchLastSection(true);
    gamesTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    gamesTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    rootLayout->addWidget(gamesTable_, 1);

    progressBar_ = new QProgressBar(centralWidget);
    progressBar_->setRange(0, 1);
    progressBar_->setValue(0);
    progressBar_->setTextVisible(false);
    rootLayout->addWidget(progressBar_);

    setCentralWidget(centralWidget);
}

void MainWindow::applyTheme()
{
    setStyleSheet(R"(
        QMainWindow, QWidget {
            background: #111315;
            color: #E9EDF1;
            font-family: "Segoe UI";
            font-size: 10pt;
        }

        QLabel#titleLabel {
            font-size: 20pt;
            font-weight: 700;
            color: #F6F8FA;
        }

        QLabel#statusLabel,
        QLabel#pathLabel {
            color: #9BA6B2;
        }

        QLabel#metricLabel {
            color: #8EE6B1;
            font-size: 14pt;
            font-weight: 700;
        }

        QFrame#toolbarFrame {
            background: #1A1D21;
            border: 1px solid #2B3138;
            border-radius: 6px;
        }

        QPushButton {
            background: #242A31;
            border: 1px solid #39414B;
            border-radius: 5px;
            color: #F4F7FA;
            min-height: 30px;
            padding: 0 14px;
        }

        QPushButton:hover {
            background: #2C3440;
            border-color: #4D9DE0;
        }

        QPushButton:disabled {
            color: #66717D;
            background: #181B1F;
            border-color: #2A3037;
        }

        QTableWidget {
            background: #15181C;
            alternate-background-color: #181C21;
            border: 1px solid #2B3138;
            border-radius: 6px;
            gridline-color: #283039;
            selection-background-color: #24425C;
            selection-color: #FFFFFF;
        }

        QHeaderView::section {
            background: #20252B;
            color: #B8C0CA;
            border: 0;
            border-right: 1px solid #303842;
            padding: 8px;
            font-weight: 600;
        }

        QProgressBar {
            background: #1A1D21;
            border: 1px solid #2B3138;
            border-radius: 4px;
            min-height: 8px;
            max-height: 8px;
        }

        QProgressBar::chunk {
            background: #4D9DE0;
            border-radius: 4px;
        }
    )");
}

void MainWindow::chooseFolder()
{
    const QString folderPath = QFileDialog::getExistingDirectory(this, "Select Game Folder");
    if (folderPath.isEmpty()) {
        return;
    }

    selectedFolder_ = folderPath;
    selectedFolderLabel_->setText(folderPath);
    startAnalysis(folderPath);
}

void MainWindow::startAnalysis(const QString& folderPath)
{
    setBusy(true);
    statusLabel_->setText("Analyzing");
    gamesTable_->setRowCount(0);
    selectedFolderLabel_->setText(folderPath);
    analysisWatcher_.setFuture(analysisController_.analyzeFolder(folderPath));
}

void MainWindow::startSteamScan()
{
    setBusy(true);
    statusLabel_->setText("Scanning Steam");
    selectedFolderLabel_->setText("Scanning Steam libraries");
    gamesTable_->setRowCount(0);
    steamScanWatcher_.setFuture(analysisController_.scanSteamGames());
}

void MainWindow::finishAnalysis()
{
    setBusy(false);

    const gsm::core::GameAnalysis analysis = analysisWatcher_.result();
    if (!analysis.isValid) {
        statusLabel_->setText("Error");
        QMessageBox::warning(this, "Analysis failed", QString::fromStdString(analysis.errorMessage));
        return;
    }

    showAnalysis(analysis);
    statusLabel_->setText("Ready");
}

void MainWindow::finishSteamScan()
{
    setBusy(false);

    const std::vector<gsm::core::GameEntry> games = steamScanWatcher_.result();
    showSteamGames(games);
    statusLabel_->setText(QString("Steam games found: %1").arg(games.size()));
}

void MainWindow::showAnalysis(const gsm::core::GameAnalysis& analysis)
{
    gsm::core::RecommendationEngine engine;
    const gsm::core::CompressionRecommendation recommendation =
        engine.recommend(analysis, gsm::core::OptimizationProfile::Balanced);

    gamesTable_->setRowCount(1);

    const QString rootPath = QString::fromStdString(analysis.rootPath);
    const QString compressedAssets = QString("%1 files / %2")
        .arg(analysis.alreadyCompressedFileCount)
        .arg(formatBytes(analysis.alreadyCompressedBytes));

    const QStringList values = {
        baseNameFromPath(rootPath),
        rootPath,
        formatBytes(analysis.totalBytes),
        QString::number(analysis.fileCount),
        compressedAssets,
        recommendationText(recommendation),
        QString::fromStdString(gsm::core::toString(recommendation.risk)),
        reasonText(recommendation)
    };

    for (int column = 0; column < values.size(); ++column) {
        auto* item = new QTableWidgetItem(values[column]);
        gamesTable_->setItem(0, column, item);
    }
}

void MainWindow::showSteamGames(const std::vector<gsm::core::GameEntry>& games)
{
    gamesTable_->setRowCount(static_cast<int>(games.size()));

    for (int row = 0; row < static_cast<int>(games.size()); ++row) {
        const gsm::core::GameEntry& game = games[static_cast<std::size_t>(row)];
        const QStringList values = {
            QString::fromStdString(game.name),
            QString::fromStdString(game.installPath),
            "Not analyzed",
            "-",
            "-",
            "Pending",
            "-",
            QString::fromStdString(gsm::core::toString(game.source))
        };

        for (int column = 0; column < values.size(); ++column) {
            auto* item = new QTableWidgetItem(values[column]);
            gamesTable_->setItem(row, column, item);
        }
    }
}

void MainWindow::setBusy(bool busy)
{
    selectFolderButton_->setEnabled(!busy);
    scanSteamButton_->setEnabled(!busy);
    analyzeButton_->setEnabled(!busy && !selectedFolder_.isEmpty());
    progressBar_->setRange(busy ? 0 : 0, busy ? 0 : 1);
    progressBar_->setValue(busy ? 0 : 1);
}

} // namespace gsm::ui
