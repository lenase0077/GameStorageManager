#include "ui/views/MainWindow.h"

#include <QFileDialog>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>

#include <QColor>
#include <QFont>
#include <QMap>

#include <windows.h>

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
        return "Pending";
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

QString MainWindow::extractDriveLetter(const QString& path)
{
    if (path.length() >= 2 && path[1] == ':') {
        return path.left(2).toUpper();
    }
    return "?";
}

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
            analyzingRow_ = -1;
            startAnalysis(selectedFolder_);
        }
    });

    connect(analyzeSelectedButton_, &QPushButton::clicked, this, [this]() {
        onAnalyzeSelected();
    });

    connect(&analysisWatcher_, &QFutureWatcher<gsm::core::GameAnalysis>::finished, this, [this]() {
        finishAnalysis();
    });

    connect(&steamScanWatcher_, &QFutureWatcher<std::vector<gsm::core::GameEntry>>::finished, this, [this]() {
        finishSteamScan();
    });

    connect(&compressWatcher_, &QFutureWatcher<gsm::core::CompressionResult>::finished, this, [this]() {
        finishCompression();
    });

    connect(&restoreWatcher_, &QFutureWatcher<gsm::core::CompressionResult>::finished, this, [this]() {
        finishRestore();
    });

    connect(optimizeButton_, &QPushButton::clicked, this, [this]() {
        onOptimize();
    });

    connect(restoreButton_, &QPushButton::clicked, this, [this]() {
        onRestore();
    });

    connect(profileCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        onProfileChanged(index);
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
    analyzeSelectedButton_ = new QPushButton("Analyze Selected", actionFrame);
    optimizeButton_ = new QPushButton("Optimize", actionFrame);
    restoreButton_ = new QPushButton("Restore", actionFrame);
    selectedFolderLabel_ = new QLabel("No folder selected", actionFrame);
    selectedFolderLabel_->setObjectName("pathLabel");

    optimizeButton_->setEnabled(false);
    restoreButton_->setEnabled(false);

    profileCombo_ = new QComboBox(actionFrame);
    profileCombo_->addItem("Fast (XPRESS4K)");
    profileCombo_->addItem("Balanced (XPRESS8K)");
    profileCombo_->addItem("Strong (XPRESS16K)");
    profileCombo_->addItem("Max (LZX)");

    actionLayout->addWidget(selectFolderButton_);
    actionLayout->addWidget(scanSteamButton_);
    actionLayout->addWidget(analyzeButton_);
    actionLayout->addWidget(analyzeSelectedButton_);
    actionLayout->addWidget(optimizeButton_);
    actionLayout->addWidget(restoreButton_);
    actionLayout->addWidget(profileCombo_);
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

        QComboBox {
            background: #242A31;
            border: 1px solid #39414B;
            border-radius: 5px;
            color: #F4F7FA;
            min-height: 30px;
            padding: 0 14px;
        }

        QComboBox:hover {
            border-color: #4D9DE0;
        }

        QComboBox QAbstractItemView {
            background: #1A1D21;
            border: 1px solid #39414B;
            color: #F4F7FA;
            selection-background-color: #24425C;
        }

        QComboBox::drop-down {
            border: 0;
            width: 24px;
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
    activeRow_ = -1;
    activeAnalysis_.reset();
    activeRecommendation_.reset();
    optimizeButton_->setEnabled(false);
    restoreButton_->setEnabled(false);
    startAnalysis(folderPath);
}

void MainWindow::startAnalysis(const QString& folderPath, const QString& gameName)
{
    setBusy(true);
    statusLabel_->setText("Analyzing");
    if (analyzingRow_ < 0) {
        gamesTable_->setRowCount(0);
    } else {
        updateRowStatus(analyzingRow_, "Analyzing...");
    }
    selectedFolderLabel_->setText(folderPath);
    analysisWatcher_.setFuture(analysisController_.analyzeFolder(folderPath, gameName));
}

void MainWindow::startSteamScan()
{
    setBusy(true);
    statusLabel_->setText("Scanning Steam");
    selectedFolderLabel_->setText("Scanning Steam libraries");
    gamesTable_->setRowCount(0);
    activeRow_ = -1;
    activeAnalysis_.reset();
    activeRecommendation_.reset();
    optimizeButton_->setEnabled(false);
    restoreButton_->setEnabled(false);
    steamScanWatcher_.setFuture(analysisController_.scanSteamGames());
}

void MainWindow::finishAnalysis()
{
    setBusy(false);

    const gsm::core::GameAnalysis analysis = analysisWatcher_.result();
    if (!analysis.isValid) {
        statusLabel_->setText("Error");
        QMessageBox::warning(this, "Analysis failed", QString::fromStdString(analysis.errorMessage));
        analyzingRow_ = -1;
        return;
    }

    if (analyzingRow_ >= 0) {
        updateGameRow(analyzingRow_, analysis);
        activeRow_ = analyzingRow_;
        analyzingRow_ = -1;
    } else {
        showAnalysis(analysis);
        activeRow_ = 0;
    }

    gsm::core::RecommendationEngine engine;
    const gsm::core::CompressionRecommendation rec =
        engine.recommendWithAlgorithm(analysis, currentAlgorithm_);
    updateActiveState(analysis, rec);
    statusLabel_->setText("Ready");
}

void MainWindow::finishSteamScan()
{
    setBusy(false);

    const std::vector<gsm::core::GameEntry> games = steamScanWatcher_.result();
    showSteamGames(games);
    applyStoredMetadata();
    statusLabel_->setText(QString("Steam games found: %1").arg(games.size()));
}

void MainWindow::showAnalysis(const gsm::core::GameAnalysis& analysis)
{
    gsm::core::RecommendationEngine engine;
    const gsm::core::CompressionRecommendation recommendation =
        engine.recommendWithAlgorithm(analysis, currentAlgorithm_);

    gamesTable_->setRowCount(1);

    const QString rootPath = QString::fromStdString(analysis.rootPath);
    const QString compressedAssets = QString("%1 ext / %2 NTFS")
        .arg(QString::number(analysis.alreadyCompressedFileCount))
        .arg(QString::number(analysis.ntfsCompressedFileCount));

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

    rowAnalyses_[0] = analysis;
    rowRecommendations_[0] = recommendation;
}

void MainWindow::showSteamGames(const std::vector<gsm::core::GameEntry>& games)
{
    auto sorted = games;
    std::sort(sorted.begin(), sorted.end(), [](const gsm::core::GameEntry& a, const gsm::core::GameEntry& b) {
        const QString da = MainWindow::extractDriveLetter(QString::fromStdString(a.installPath));
        const QString db = MainWindow::extractDriveLetter(QString::fromStdString(b.installPath));
        if (da != db) return da < db;
        return a.name < b.name;
    });

    QMap<QString, int> gamesPerDrive;
    for (const auto& game : sorted) {
        gamesPerDrive[MainWindow::extractDriveLetter(QString::fromStdString(game.installPath))]++;
    }

    gamesTable_->clearSpans();
    gamesTable_->setRowCount(0);

    constexpr int kDriveHeaderBgRole = Qt::UserRole + 1;
    constexpr int kPathRole = Qt::UserRole;

    QString currentDrive;
    int totalRows = 0;

    for (const auto& game : sorted) {
        const QString drive = MainWindow::extractDriveLetter(QString::fromStdString(game.installPath));
        if (drive != currentDrive) {
            currentDrive = drive;
            const int driveRow = totalRows++;
            gamesTable_->insertRow(driveRow);

            const QString label = QString("%1:  (%2 games)%3")
                .arg(drive,
                     QString::number(gamesPerDrive.value(drive)),
                     MainWindow::driveSpaceInfo(drive));
            auto* headerItem = new QTableWidgetItem(label);
            headerItem->setBackground(QColor("#1E3550"));
            headerItem->setForeground(QColor("#8EC8F2"));
            QFont boldFont = headerItem->font();
            boldFont.setBold(true);
            boldFont.setPointSize(boldFont.pointSize() + 1);
            headerItem->setFont(boldFont);
            headerItem->setFlags(Qt::NoItemFlags);
            headerItem->setData(kDriveHeaderBgRole, true);
            gamesTable_->setItem(driveRow, 0, headerItem);
            gamesTable_->setSpan(driveRow, 0, 1, gamesTable_->columnCount());

            for (int col = 1; col < gamesTable_->columnCount(); ++col) {
                auto* filler = new QTableWidgetItem("");
                filler->setBackground(QColor("#1E3550"));
                filler->setFlags(Qt::NoItemFlags);
                filler->setData(kDriveHeaderBgRole, true);
                gamesTable_->setItem(driveRow, col, filler);
            }
        }

        const int gameRow = totalRows++;
        gamesTable_->insertRow(gameRow);

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

        const QString path = QString::fromStdString(game.installPath);
        for (int col = 0; col < values.size(); ++col) {
            auto* item = new QTableWidgetItem(values[col]);
            item->setData(kPathRole, path);
            item->setData(kDriveHeaderBgRole, false);
            gamesTable_->setItem(gameRow, col, item);
        }
    }

    analyzingRow_ = -1;
}

void MainWindow::onAnalyzeSelected()
{
    const int currentRow = gamesTable_->currentRow();
    if (currentRow < 0 || currentRow >= gamesTable_->rowCount()) {
        QMessageBox::information(this, "No selection", "Select a game row first.");
        return;
    }

    auto* item = gamesTable_->item(currentRow, 0);
    if (!item) return;

    constexpr int kDriveHeaderBgRole = Qt::UserRole + 1;
    if (item->data(kDriveHeaderBgRole).toBool()) {
        QMessageBox::information(this, "Cannot analyze", "Select a game row, not a drive header.");
        return;
    }

    const QString gameName = item->text();
    const QString path = item->data(Qt::UserRole).toString();
    if (path.isEmpty()) return;

    analyzingRow_ = currentRow;
    startAnalysis(path, gameName);
}

void MainWindow::updateGameRow(int row, const gsm::core::GameAnalysis& analysis)
{
    gsm::core::RecommendationEngine engine;
    const gsm::core::CompressionRecommendation rec =
        engine.recommendWithAlgorithm(analysis, currentAlgorithm_);

    const QString compressedAssets = QString("%1 ext / %2 NTFS")
        .arg(analysis.alreadyCompressedFileCount)
        .arg(analysis.ntfsCompressedFileCount);

    const QStringList values = {
        baseNameFromPath(QString::fromStdString(analysis.rootPath)),
        QString::fromStdString(analysis.rootPath),
        formatBytes(analysis.totalBytes),
        QString::number(analysis.fileCount),
        compressedAssets,
        recommendationText(rec),
        QString::fromStdString(gsm::core::toString(rec.risk)),
        reasonText(rec)
    };

    const QString path = QString::fromStdString(analysis.rootPath);
    for (int col = 0; col < values.size(); ++col) {
        auto* item = gamesTable_->item(row, col);
        if (!item) {
            item = new QTableWidgetItem(values[col]);
            gamesTable_->setItem(row, col, item);
        } else {
            item->setText(values[col]);
        }
        item->setData(Qt::UserRole, path);
    }

    rowAnalyses_[row] = analysis;
    rowRecommendations_[row] = rec;
}

QString MainWindow::driveSpaceInfo(const QString& driveLetter)
{
    const std::string rootPath = driveLetter.toStdString() + "\\";
    ULARGE_INTEGER freeBytesAvailable{};
    ULARGE_INTEGER totalBytes{};
    ULARGE_INTEGER totalFreeBytes{};

    if (!GetDiskFreeSpaceExA(rootPath.c_str(), &freeBytesAvailable, &totalBytes, &totalFreeBytes)) {
        return {};
    }

    const auto total = static_cast<std::uintmax_t>(totalBytes.QuadPart);
    const auto free = static_cast<std::uintmax_t>(totalFreeBytes.QuadPart);

    return QString("  [%1 free / %2 total]")
        .arg(formatBytes(free), formatBytes(total));
}

void MainWindow::applyStoredMetadata()
{
    const char* localAppData = std::getenv("LOCALAPPDATA");
    const std::string storageRoot = localAppData
        ? gsm::system::joinPath(localAppData, "GameStorageManager/metadata")
        : "metadata";

    gsm::core::SafetyMetadataStore store(storageRoot);
    const auto allMetadata = store.loadAll();

    std::map<std::string, gsm::core::SafetyMetadata> metadataByPath;
    for (const auto& meta : allMetadata) {
        metadataByPath[gsm::system::normalizePath(meta.rootPath)] = meta;
    }

    std::uintmax_t totalSaved = 0;

    for (int row = 0; row < gamesTable_->rowCount(); ++row) {
        auto* item = gamesTable_->item(row, 0);
        if (!item) continue;

        const bool isHeader = item->data(Qt::UserRole + 1).toBool();
        if (isHeader) continue;

        const QString pathStr = item->data(Qt::UserRole).toString();
        const std::string normalizedPath = gsm::system::normalizePath(pathStr.toStdString());

        auto it = metadataByPath.find(normalizedPath);
        if (it == metadataByPath.end()) continue;

        const auto& meta = it->second;

        switch (meta.state) {
        case gsm::core::SafetyOperationState::Completed: {
            const auto saved = meta.sizeBeforeBytes > meta.sizeAfterBytes
                ? meta.sizeBeforeBytes - meta.sizeAfterBytes : 0;
            totalSaved += saved;
            updateRowStatus(row, QString("Optimized (%1)").arg(formatBytes(saved)));

            auto* sizeItem = gamesTable_->item(row, 2);
            if (sizeItem) {
                sizeItem->setText(formatBytes(meta.sizeAfterBytes));
            }

            auto* recItem = gamesTable_->item(row, 5);
            if (recItem && meta.algorithm.has_value()) {
                recItem->setText(QString::fromStdString(gsm::core::toString(*meta.algorithm)));
            }
            break;
        }
        case gsm::core::SafetyOperationState::Restored:
            updateRowStatus(row, "Restored");
            break;
        case gsm::core::SafetyOperationState::Failed:
            updateRowStatus(row, "Optimization failed");
            break;
        case gsm::core::SafetyOperationState::Planned:
            updateRowStatus(row, "Planned (not executed)");
            break;
        default:
            break;
        }
    }

    if (totalSaved > 0) {
        totalSavedLabel_->setText(QString("Total saved: %1").arg(formatBytes(totalSaved)));
    }
}

void MainWindow::updateActiveRowFromMetadata(const std::string& normalizedPath)
{
    const char* localAppData = std::getenv("LOCALAPPDATA");
    const std::string storageRoot = localAppData
        ? gsm::system::joinPath(localAppData, "GameStorageManager/metadata")
        : "metadata";

    gsm::core::SafetyMetadataStore store(storageRoot);
    const auto metadata = store.loadById(gsm::core::SafetyMetadataStore::makeStableId(normalizedPath));

    if (metadata.has_value() && metadata->state == gsm::core::SafetyOperationState::Completed) {
        optimizeButton_->setEnabled(false);
        restoreButton_->setEnabled(true);

        const auto saved = metadata->sizeBeforeBytes > metadata->sizeAfterBytes
            ? metadata->sizeBeforeBytes - metadata->sizeAfterBytes : 0;
        totalSavedLabel_->setText(QString("Saved: %1").arg(formatBytes(saved)));
    }
}

void MainWindow::onProfileChanged(int index)
{
    switch (index) {
    case 0: currentAlgorithm_ = gsm::core::CompressionAlgorithm::Xpress4k; break;
    case 1: currentAlgorithm_ = gsm::core::CompressionAlgorithm::Xpress8k; break;
    case 2: currentAlgorithm_ = gsm::core::CompressionAlgorithm::Xpress16k; break;
    case 3: currentAlgorithm_ = gsm::core::CompressionAlgorithm::Lzx; break;
    default: return;
    }

    if (!activeAnalysis_.has_value()) return;

    gsm::core::RecommendationEngine engine;
    const gsm::core::CompressionRecommendation rec =
        engine.recommendWithAlgorithm(*activeAnalysis_, currentAlgorithm_);
    activeRecommendation_ = rec;

    updateActiveState(*activeAnalysis_, rec);

    if (activeRow_ >= 0) {
        auto* recItem = gamesTable_->item(activeRow_, 5);
        if (recItem) {
            recItem->setText(recommendationText(rec));
        }
        auto* riskItem = gamesTable_->item(activeRow_, 6);
        if (riskItem) {
            riskItem->setText(QString::fromStdString(gsm::core::toString(rec.risk)));
        }
        auto* statusItem = gamesTable_->item(activeRow_, 7);
        if (statusItem) {
            statusItem->setText(reasonText(rec));
        }
    }
}

bool MainWindow::tryGetRowAnalysis(int row, gsm::core::GameAnalysis& outAnalysis,
                                    gsm::core::CompressionRecommendation& outRecommendation) const
{
    if (row < 0 || row >= gamesTable_->rowCount()) return false;

    auto* item = gamesTable_->item(row, 0);
    if (!item || item->data(Qt::UserRole + 1).toBool()) return false;

    auto itA = rowAnalyses_.find(row);
    auto itR = rowRecommendations_.find(row);
    if (itA == rowAnalyses_.end() || itR == rowRecommendations_.end()) return false;

    outAnalysis = itA->second;
    outRecommendation = itR->second;
    return true;
}

void MainWindow::updateActiveState(const gsm::core::GameAnalysis& analysis,
                                    const gsm::core::CompressionRecommendation& recommendation)
{
    activeAnalysis_ = analysis;
    activeRecommendation_ = recommendation;

    updateActiveRowFromMetadata(gsm::system::normalizePath(analysis.rootPath));

    if (optimizeButton_->isEnabled() || restoreButton_->isEnabled()) return;

    optimizeButton_->setEnabled(recommendation.action == gsm::core::RecommendationAction::Compress);
    restoreButton_->setEnabled(false);
}

void MainWindow::updateRowStatus(int row, const QString& status)
{
    constexpr int kStatusColumn = 7;
    auto* item = gamesTable_->item(row, kStatusColumn);
    if (item) {
        item->setText(status);
    }
}

void MainWindow::onOptimize()
{
    const int selectedRow = gamesTable_->currentRow();

    gsm::core::GameAnalysis analysis;
    gsm::core::CompressionRecommendation recommendation;

    if (selectedRow >= 0) {
        if (!tryGetRowAnalysis(selectedRow, analysis, recommendation)) {
            QMessageBox::information(this, "Not analyzed",
                "Analyze this game first (click Analyze Selected).");
            return;
        }
        activeRow_ = selectedRow;
        activeAnalysis_ = analysis;
        activeRecommendation_ = recommendation;
    } else if (activeAnalysis_.has_value() && activeRecommendation_.has_value()) {
        analysis = *activeAnalysis_;
        recommendation = *activeRecommendation_;
    } else {
        QMessageBox::information(this, "No game", "Analyze a game first or select an analyzed row.");
        return;
    }

    if (recommendation.action != gsm::core::RecommendationAction::Compress) {
        QMessageBox::information(this, "Skipped", "This game is marked as Skip.");
        return;
    }

    setBusy(true);
    statusLabel_->setText("Optimizing");

    if (activeRow_ >= 0) {
        updateRowStatus(activeRow_, "Optimizing");
    }

    compressWatcher_.setFuture(
        compressionController_.compress(analysis, recommendation));
}

void MainWindow::finishCompression()
{
    setBusy(false);

    const gsm::core::CompressionResult result = compressWatcher_.result();
    if (!result.success) {
        statusLabel_->setText("Error");
        if (activeRow_ >= 0) updateRowStatus(activeRow_, "Error");
        QMessageBox::warning(this, "Compression failed",
            QString::fromStdString(result.errorMessage.empty() ? result.output : result.errorMessage));
        return;
    }

    const auto saved = result.bytesBefore > result.bytesAfter
        ? result.bytesBefore - result.bytesAfter : 0;

    if (saved == 0) {
        const auto& m = result.metrics;
        QString detail;
        if (m.filesAlreadyCompressed > 0 && m.filesCompressed == 0) {
            detail = QString("All %1 files already compressed. No savings possible.")
                .arg(m.filesAlreadyCompressed);
        } else {
            detail = "No files compressed. Folder is already NTFS-compressed or contains only incompressible data.";
        }
        statusLabel_->setText(detail);
        statusLabel_->setText(detail);
        if (activeRow_ >= 0) updateRowStatus(activeRow_, "No change");
        QMessageBox::information(this, "No savings", detail);
        optimizeButton_->setEnabled(true);
        restoreButton_->setEnabled(false);
        return;
    }

    const QString savedStr = formatBytes(saved);
    statusLabel_->setText(QString("Optimized — saved %1").arg(savedStr));

    if (activeRow_ >= 0) {
        updateRowStatus(activeRow_, QString("Optimized (%1 saved)").arg(savedStr));
        auto* sizeItem = gamesTable_->item(activeRow_, 2);
        if (sizeItem) sizeItem->setText(formatBytes(result.bytesAfter));
    }

    totalSavedLabel_->setText(QString("Saved: %1").arg(savedStr));
    optimizeButton_->setEnabled(false);
    restoreButton_->setEnabled(true);
}

void MainWindow::onRestore()
{
    if (!activeAnalysis_.has_value()) {
        QMessageBox::information(this, "No game", "Analyze or select an optimized game first.");
        return;
    }

    setBusy(true);
    statusLabel_->setText("Restoring");

    if (activeRow_ >= 0) updateRowStatus(activeRow_, "Restoring");

    const std::string id = gsm::core::SafetyMetadataStore::makeStableId(activeAnalysis_->rootPath);
    const gsm::system::Path path = activeAnalysis_->rootPath;

    gsm::core::SafetyMetadata metadata;
    metadata.id = id;
    metadata.rootPath = path;

    restoreWatcher_.setFuture(compressionController_.restore(metadata));
}

void MainWindow::finishRestore()
{
    setBusy(false);

    const gsm::core::CompressionResult result = restoreWatcher_.result();
    if (!result.success) {
        statusLabel_->setText("Restore error");
        if (activeRow_ >= 0) updateRowStatus(activeRow_, "Restore error");
        QMessageBox::warning(this, "Restore failed",
            QString::fromStdString(result.errorMessage.empty() ? result.output : result.errorMessage));
        return;
    }

    statusLabel_->setText("Restored");
    if (activeRow_ >= 0) {
        updateRowStatus(activeRow_, "Restored");
        auto* sizeItem = gamesTable_->item(activeRow_, 2);
        if (sizeItem) sizeItem->setText(formatBytes(result.bytesAfter));
    }

    totalSavedLabel_->setText("Saved: 0 B");
    optimizeButton_->setEnabled(true);
    restoreButton_->setEnabled(false);
}

void MainWindow::setBusy(bool busy)
{
    selectFolderButton_->setEnabled(!busy);
    scanSteamButton_->setEnabled(!busy);
    analyzeButton_->setEnabled(!busy && !selectedFolder_.isEmpty());
    analyzeSelectedButton_->setEnabled(!busy);
    if (busy) {
        optimizeButton_->setEnabled(false);
        restoreButton_->setEnabled(false);
    }
    profileCombo_->setEnabled(!busy);
    progressBar_->setRange(busy ? 0 : 0, busy ? 0 : 1);
    progressBar_->setValue(busy ? 0 : 1);
}

} // namespace gsm::ui
