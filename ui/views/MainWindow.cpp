#include "ui/views/MainWindow.h"
#include "ui/views/SettingsDialog.h"

#include <QFileDialog>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QWidget>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <QFile>
#include <QDir>

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

    connect(analyzeAllButton_, &QPushButton::clicked, this, [this]() {
        onAnalyzeAll();
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

    connect(removeButton_, &QPushButton::clicked, this, [this]() {
        onRemoveGame();
    });

    connect(settingsButton_, &QPushButton::clicked, this, [this]() {
        onSettings();
    });

    connect(cancelButton_, &QPushButton::clicked, this, [this]() {
        analysisController_.cancel();
        compressionController_.cancel();
        pendingAnalysisRows_.clear();
        statusLabel_->setText("Cancelling...");
        cancelButton_->setEnabled(false);
    });

    connect(profileCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        onProfileChanged(index);
    });

    setBusy(false);
    loadLibrary();
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

    auto* metricsFrame = new QFrame(centralWidget);
    metricsFrame->setObjectName("metricsFrame");
    auto* metricsLayout = new QHBoxLayout(metricsFrame);
    metricsLayout->setContentsMargins(16, 8, 16, 8);
    metricsLayout->setSpacing(20);

    gamesCountLabel_ = new QLabel("🎮 0 Optimized", metricsFrame);
    gamesCountLabel_->setObjectName("metricValue");

    spaceSavedLabel_ = new QLabel("🔥 0 B Saved", metricsFrame);
    spaceSavedLabel_->setObjectName("metricValue");

    ratioLabel_ = new QLabel("📊 0.0% Avg Ratio", metricsFrame);
    ratioLabel_->setObjectName("metricValue");

    metricsLayout->addWidget(gamesCountLabel_);
    metricsLayout->addWidget(spaceSavedLabel_);
    metricsLayout->addWidget(ratioLabel_);

    headerLayout->addWidget(metricsFrame, 0, Qt::AlignRight | Qt::AlignVCenter);
    rootLayout->addLayout(headerLayout);

    auto* actionFrame = new QFrame(centralWidget);
    actionFrame->setObjectName("toolbarFrame");
    auto* actionLayout = new QHBoxLayout(actionFrame);
    actionLayout->setContentsMargins(14, 12, 14, 12);
    actionLayout->setSpacing(10);

    settingsButton_ = new QPushButton("⚙️ Settings", actionFrame);
    selectFolderButton_ = new QPushButton("Add Game Folder", actionFrame);
    scanSteamButton_ = new QPushButton("Scan Steam", actionFrame);
    analyzeSelectedButton_ = new QPushButton("Analyze Selected", actionFrame);
    analyzeAllButton_ = new QPushButton("Analyze All", actionFrame);
    optimizeButton_ = new QPushButton("Optimize", actionFrame);
    restoreButton_ = new QPushButton("Restore", actionFrame);
    removeButton_ = new QPushButton("Remove", actionFrame);
    cancelButton_ = new QPushButton("Cancel", actionFrame);
    selectedFolderLabel_ = new QLabel("No folder selected", actionFrame);
    selectedFolderLabel_->setObjectName("pathLabel");

    optimizeButton_->setEnabled(false);
    restoreButton_->setEnabled(false);
    cancelButton_->setEnabled(false);

    profileCombo_ = new QComboBox(actionFrame);
    profileCombo_->addItem("Fast (XPRESS4K)");
    profileCombo_->addItem("Balanced (XPRESS8K)");
    profileCombo_->addItem("Strong (XPRESS16K)");
    profileCombo_->addItem("Max (LZX)");
    
    QSettings settings("GameStorageManager", "App");
    int defaultAlgo = settings.value("defaultAlgorithm", 1).toInt();
    if (defaultAlgo >= 0 && defaultAlgo < profileCombo_->count()) {
        profileCombo_->setCurrentIndex(defaultAlgo);
    }

    actionLayout->addWidget(settingsButton_);
    actionLayout->addWidget(selectFolderButton_);
    actionLayout->addWidget(scanSteamButton_);
    actionLayout->addWidget(analyzeSelectedButton_);
    actionLayout->addWidget(analyzeAllButton_);
    actionLayout->addWidget(optimizeButton_);
    actionLayout->addWidget(restoreButton_);
    actionLayout->addWidget(removeButton_);
    actionLayout->addWidget(cancelButton_);
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

        QFrame#metricsFrame {
            background: #16241D;
            border: 1px solid #2B4D36;
            border-radius: 8px;
        }
        
        QLabel#metricValue {
            color: #8EE6B1;
            font-size: 11pt;
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

    std::string pathStr = folderPath.toStdString();
    bool exists = false;
    for (const auto& g : libraryGames_) {
        if (QString::fromStdString(g.installPath).compare(folderPath, Qt::CaseInsensitive) == 0) {
            exists = true;
            break;
        }
    }
    
    if (!exists) {
        gsm::core::GameEntry entry;
        entry.name = baseNameFromPath(folderPath).toStdString();
        entry.installPath = pathStr;
        entry.source = gsm::core::GameSource::Manual;
        libraryGames_.push_back(entry);
        saveLibrary();
        refreshTableView();
    }

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

    analyzingRow_ = -1;
    for (int r = 0; r < gamesTable_->rowCount(); ++r) {
        auto* item = gamesTable_->item(r, 0);
        if (item && !item->data(Qt::UserRole + 1).toBool()) {
            if (item->data(Qt::UserRole).toString().compare(folderPath, Qt::CaseInsensitive) == 0) {
                analyzingRow_ = r;
                break;
            }
        }
    }

    if (analyzingRow_ >= 0) {
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

    if (analyzingRow_ < 0) {
        for (int r = 0; r < gamesTable_->rowCount(); ++r) {
            auto* item = gamesTable_->item(r, 0);
            if (item && !item->data(Qt::UserRole + 1).toBool()) {
                if (item->data(Qt::UserRole).toString().compare(QString::fromStdString(analysis.rootPath), Qt::CaseInsensitive) == 0) {
                    analyzingRow_ = r;
                    break;
                }
            }
        }
    }

    if (analyzingRow_ >= 0) {
        updateGameRow(analyzingRow_, analysis);
        activeRow_ = analyzingRow_;
        analyzingRow_ = -1;
    } else {
        showAnalysis(analysis);
        activeRow_ = gamesTable_->rowCount() - 1;
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
    
    for (const auto& g : games) {
        bool exists = false;
        for (const auto& libG : libraryGames_) {
            if (QString::fromStdString(libG.installPath).compare(QString::fromStdString(g.installPath), Qt::CaseInsensitive) == 0) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            libraryGames_.push_back(g);
        }
    }
    
    saveLibrary();
    refreshTableView();
    applyStoredMetadata();
    statusLabel_->setText(QString("Steam games found: %1").arg(games.size()));
}

void MainWindow::showAnalysis(const gsm::core::GameAnalysis& analysis)
{
    gsm::core::RecommendationEngine engine;
    const gsm::core::CompressionRecommendation recommendation =
        engine.recommendWithAlgorithm(analysis, currentAlgorithm_);

    int row = gamesTable_->rowCount();
    gamesTable_->insertRow(row);

    const QString rootPath = QString::fromStdString(analysis.rootPath);
    const QString compressedAssets = QString("%1 ext / %2 NTFS")
        .arg(QString::number(analysis.alreadyCompressedFileCount))
        .arg(QString::number(analysis.ntfsCompressedFileCount));

    gsm::core::GameSource source = gsm::core::GameSource::Manual;
    for (const auto& g : libraryGames_) {
        if (QString::fromStdString(g.installPath).compare(rootPath, Qt::CaseInsensitive) == 0) {
            source = g.source;
            break;
        }
    }

    const QString prefix = (source == gsm::core::GameSource::Steam) ? "🎮 " : "📁 ";
    
    QString sizeText = formatBytes(analysis.totalBytes);
    QString statusText = reasonText(recommendation);
    bool isOptimized = false;

    if (analysis.totalBytes < analysis.logicalBytes && analysis.ntfsCompressedFileCount > 0) {
        const auto saved = analysis.logicalBytes - analysis.totalBytes;
        if (saved > 1024 * 1024) { // Ignore < 1MB differences
            sizeText = QString("%1 (-%2)").arg(formatBytes(analysis.totalBytes), formatBytes(saved));
            statusText = QString("Optimized (%1 saved)").arg(formatBytes(saved));
            isOptimized = true;
        }
    }

    const QStringList values = {
        prefix + baseNameFromPath(rootPath),
        rootPath,
        sizeText,
        QString::number(analysis.fileCount),
        compressedAssets,
        recommendationText(recommendation),
        QString::fromStdString(gsm::core::toString(recommendation.risk)),
        statusText
    };

    for (int column = 0; column < values.size(); ++column) {
        auto* item = new QTableWidgetItem(values[column]);
        item->setData(Qt::UserRole, rootPath);
        item->setData(Qt::UserRole + 1, false);

        if (column == 0) {
            if (source == gsm::core::GameSource::Steam) {
                item->setForeground(QColor("#4D9DE0"));
                item->setToolTip("Source: Steam");
            } else {
                item->setForeground(QColor("#8EE6B1"));
                item->setToolTip("Source: Manual");
            }
        }
        
        if (column == 7) {
            if (isOptimized) {
                item->setForeground(QColor("#8EE6B1"));
            } else {
                item->setForeground(QColor("#E9EDF1"));
            }
        }

        gamesTable_->setItem(row, column, item);
    }

    rowAnalyses_[row] = analysis;
    rowRecommendations_[row] = recommendation;
}

void MainWindow::refreshTableView()
{
    activeRow_ = -1;
    activeAnalysis_.reset();
    activeRecommendation_.reset();
    rowAnalyses_.clear();
    rowRecommendations_.clear();

    auto sorted = libraryGames_;
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

        const QString path = QString::fromStdString(game.installPath);
        bool folderExists = QDir(path).exists();
        
        const QString prefix = (game.source == gsm::core::GameSource::Steam) ? "🎮 " : "📁 ";
        const QStringList values = {
            prefix + QString::fromStdString(game.name),
            QString::fromStdString(game.installPath),
            "Not analyzed",
            "-",
            "-",
            "Pending",
            "-",
            folderExists ? QString::fromStdString(gsm::core::toString(game.source)) : "Not found (Missing)"
        };

        for (int col = 0; col < values.size(); ++col) {
            auto* item = new QTableWidgetItem(values[col]);
            item->setData(kPathRole, path);
            item->setData(kDriveHeaderBgRole, false);

            if (col == 0) {
                if (game.source == gsm::core::GameSource::Steam) {
                    item->setForeground(QColor("#4D9DE0"));
                    item->setToolTip("Source: Steam");
                } else {
                    item->setForeground(QColor("#8EE6B1"));
                    item->setToolTip("Source: Manual");
                }
            }
            
            if (!folderExists) {
                item->setForeground(QColor("#F85149")); // Red for missing
            }

            gamesTable_->setItem(gameRow, col, item);
        }
    }

    analyzingRow_ = -1;
}

void MainWindow::loadLibrary()
{
    const char* localAppData = std::getenv("LOCALAPPDATA");
    const QString storageRoot = localAppData
        ? QString::fromStdString(gsm::system::joinPath(localAppData, "GameStorageManager/metadata"))
        : "metadata";

    QDir().mkpath(storageRoot);
    QFile file(storageRoot + "/library.json");
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isArray()) {
        QJsonArray arr = doc.array();
        libraryGames_.clear();
        for (const auto& val : arr) {
            QJsonObject obj = val.toObject();
            gsm::core::GameEntry entry;
            entry.name = obj["name"].toString().toStdString();
            entry.installPath = obj["installPath"].toString().toStdString();
            
            QString src = obj["source"].toString();
            if (src == "Steam") entry.source = gsm::core::GameSource::Steam;
            else if (src == "Epic") entry.source = gsm::core::GameSource::Epic;
            else entry.source = gsm::core::GameSource::Manual;

            libraryGames_.push_back(entry);
        }
    }
    
    refreshTableView();
}

void MainWindow::saveLibrary()
{
    const char* localAppData = std::getenv("LOCALAPPDATA");
    const QString storageRoot = localAppData
        ? QString::fromStdString(gsm::system::joinPath(localAppData, "GameStorageManager/metadata"))
        : "metadata";

    QDir().mkpath(storageRoot);
    QFile file(storageRoot + "/library.json");
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }

    QJsonArray arr;
    for (const auto& g : libraryGames_) {
        QJsonObject obj;
        obj["name"] = QString::fromStdString(g.name);
        obj["installPath"] = QString::fromStdString(g.installPath);
        obj["source"] = QString::fromStdString(gsm::core::toString(g.source));
        arr.append(obj);
    }

    QJsonDocument doc(arr);
    file.write(doc.toJson());
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

    if (!QDir(path).exists()) {
        QMessageBox::warning(this, "Folder Missing", "The game folder no longer exists. It may have been uninstalled or moved.");
        updateRowStatus(currentRow, "Not found (Missing)");
        return;
    }

    analyzingRow_ = currentRow;
    startAnalysis(path, gameName);
}

void MainWindow::onAnalyzeAll()
{
    pendingAnalysisRows_.clear();
    for (int r = 0; r < gamesTable_->rowCount(); ++r) {
        auto* item = gamesTable_->item(r, 0);
        if (item && !item->data(Qt::UserRole + 1).toBool()) {
            pendingAnalysisRows_.push_back(r);
        }
    }
    
    if (!pendingAnalysisRows_.empty()) {
        processNextAnalysis();
    } else {
        QMessageBox::information(this, "Analyze All", "No games to analyze in the library.");
    }
}

void MainWindow::processNextAnalysis()
{
    if (pendingAnalysisRows_.empty()) {
        statusLabel_->setText("Ready");
        return;
    }

    int row = pendingAnalysisRows_.front();
    pendingAnalysisRows_.erase(pendingAnalysisRows_.begin());

    auto* item = gamesTable_->item(row, 0);
    if (!item) {
        processNextAnalysis(); // Skip invalid
        return;
    }

    const QString gameName = item->text();
    const QString path = item->data(Qt::UserRole).toString();

    if (!QDir(path).exists()) {
        updateRowStatus(row, "Not found (Missing)");
        processNextAnalysis(); // Skip missing
        return;
    }

    // Scroll to the row being analyzed so the user sees progress
    gamesTable_->scrollToItem(item);
    gamesTable_->selectRow(row);
    activeRow_ = row;

    analyzingRow_ = row;
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

    const QString path = QString::fromStdString(analysis.rootPath);
    
    gsm::core::GameSource source = gsm::core::GameSource::Manual;
    for (const auto& g : libraryGames_) {
        if (QString::fromStdString(g.installPath).compare(path, Qt::CaseInsensitive) == 0) {
            source = g.source;
            break;
        }
    }

    const QString prefix = (source == gsm::core::GameSource::Steam) ? "🎮 " : "📁 ";
    
    QString sizeText = formatBytes(analysis.totalBytes);
    QString statusText = reasonText(rec);
    bool isOptimized = false;

    if (analysis.totalBytes < analysis.logicalBytes && analysis.ntfsCompressedFileCount > 0) {
        const auto saved = analysis.logicalBytes - analysis.totalBytes;
        if (saved > 1024 * 1024) { // Ignore < 1MB differences
            sizeText = QString("%1 (-%2)").arg(formatBytes(analysis.totalBytes), formatBytes(saved));
            statusText = QString("Optimized (%1 saved)").arg(formatBytes(saved));
            isOptimized = true;
        }
    }

    const QStringList values = {
        prefix + baseNameFromPath(path),
        path,
        sizeText,
        QString::number(analysis.fileCount),
        compressedAssets,
        recommendationText(rec),
        QString::fromStdString(gsm::core::toString(rec.risk)),
        statusText
    };

    for (int col = 0; col < values.size(); ++col) {
        auto* item = gamesTable_->item(row, col);
        if (!item) {
            item = new QTableWidgetItem(values[col]);
            gamesTable_->setItem(row, col, item);
        } else {
            item->setText(values[col]);
        }
        item->setData(Qt::UserRole, path);

        if (col == 0) {
            if (source == gsm::core::GameSource::Steam) {
                item->setForeground(QColor("#4D9DE0"));
                item->setToolTip("Source: Steam");
            } else {
                item->setForeground(QColor("#8EE6B1"));
                item->setToolTip("Source: Manual");
            }
        }
        
        if (col == 7) {
            if (isOptimized) {
                item->setForeground(QColor("#8EE6B1"));
            } else {
                item->setForeground(QColor("#E9EDF1"));
            }
        }
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
    int gamesOptimized = 0;
    std::uintmax_t totalBeforeBytes = 0;
    std::uintmax_t totalAfterBytes = 0;

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
            gamesOptimized++;
            totalBeforeBytes += meta.sizeBeforeBytes;
            totalAfterBytes += meta.sizeAfterBytes;
            
            updateRowStatus(row, QString("Optimized (%1)").arg(formatBytes(saved)));

            // Highlight status in green
            auto* statusItem = gamesTable_->item(row, 7);
            if (statusItem) {
                statusItem->setForeground(QColor("#8EE6B1"));
            }

            auto* sizeItem = gamesTable_->item(row, 2);
            if (sizeItem) {
                sizeItem->setText(QString("%1 (-%2)").arg(formatBytes(meta.sizeAfterBytes), formatBytes(saved)));
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

    gamesCountLabel_->setText(QString("🎮 %1 Optimized").arg(gamesOptimized));
    spaceSavedLabel_->setText(QString("🔥 %1 Saved").arg(formatBytes(totalSaved)));

    double ratio = 0.0;
    if (totalBeforeBytes > 0) {
        ratio = (static_cast<double>(totalBeforeBytes - totalAfterBytes) / totalBeforeBytes) * 100.0;
    }
    ratioLabel_->setText(QString("📊 %1% Avg Ratio").arg(QString::number(ratio, 'f', 1)));
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

    gsm::core::RecommendationEngine engine;

    for (auto& [row, analysis] : rowAnalyses_) {
        const gsm::core::CompressionRecommendation rec = engine.recommendWithAlgorithm(analysis, currentAlgorithm_);
        rowRecommendations_[row] = rec;

        auto* recItem = gamesTable_->item(row, 5);
        if (recItem) {
            recItem->setText(recommendationText(rec));
        }
        auto* riskItem = gamesTable_->item(row, 6);
        if (riskItem) {
            riskItem->setText(QString::fromStdString(gsm::core::toString(rec.risk)));
        }
        auto* statusItem = gamesTable_->item(row, 7);
        if (statusItem) {
            statusItem->setText(reasonText(rec));
        }
    }

    if (activeAnalysis_.has_value()) {
        const gsm::core::CompressionRecommendation rec = engine.recommendWithAlgorithm(*activeAnalysis_, currentAlgorithm_);
        activeRecommendation_ = rec;
        updateActiveState(*activeAnalysis_, rec);
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

    optimizeButton_->setEnabled(recommendation.action == gsm::core::RecommendationAction::Compress);
    
    // Reality-based restore button: if NTFS compression exists on disk, we can restore it!
    bool hasCompression = analysis.ntfsCompressedFileCount > 0 || analysis.totalBytes < analysis.logicalBytes;
    restoreButton_->setEnabled(hasCompression);

    if (hasCompression && analysis.totalBytes < analysis.logicalBytes) {
        // We only show metrics via applyStoredMetadata now
    }
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
    
    progressBar_->setMaximum(static_cast<int>(analysis.fileCount));
    progressBar_->setValue(0);

    if (activeRow_ >= 0) {
        updateRowStatus(activeRow_, "Optimizing");
    }

    auto onProgress = [this](size_t linesProcessed) {
        QMetaObject::invokeMethod(this, [this, linesProcessed]() {
            progressBar_->setValue(static_cast<int>(linesProcessed));
        }, Qt::QueuedConnection);
    };

    compressWatcher_.setFuture(
        compressionController_.compress(analysis, recommendation, onProgress));
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
        if (sizeItem) sizeItem->setText(QString("%1 (-%2)").arg(formatBytes(result.bytesAfter), savedStr));
        auto* statusItem = gamesTable_->item(activeRow_, 7);
        if (statusItem) statusItem->setForeground(QColor("#8EE6B1"));
    }

    applyStoredMetadata(); // update the metrics UI
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
    
    progressBar_->setMaximum(static_cast<int>(activeAnalysis_->fileCount));
    progressBar_->setValue(0);

    if (activeRow_ >= 0) updateRowStatus(activeRow_, "Restoring");

    const std::string id = gsm::core::SafetyMetadataStore::makeStableId(activeAnalysis_->rootPath);
    const gsm::system::Path path = activeAnalysis_->rootPath;

    gsm::core::SafetyMetadata metadata;
    metadata.id = id;
    metadata.rootPath = path;

    auto onProgress = [this](size_t linesProcessed) {
        QMetaObject::invokeMethod(this, [this, linesProcessed]() {
            progressBar_->setValue(static_cast<int>(linesProcessed));
        }, Qt::QueuedConnection);
    };

    restoreWatcher_.setFuture(compressionController_.restore(metadata, onProgress));
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
        auto* statusItem = gamesTable_->item(activeRow_, 7);
        if (statusItem) statusItem->setForeground(QColor("#E9EDF1")); // reset color
    }

    applyStoredMetadata(); // update the metrics UI
    optimizeButton_->setEnabled(true);
    restoreButton_->setEnabled(false);
}

void MainWindow::onRemoveGame()
{
    const int currentRow = gamesTable_->currentRow();
    if (currentRow < 0 || currentRow >= gamesTable_->rowCount()) {
        QMessageBox::information(this, "No selection", "Select a game row to remove.");
        return;
    }

    auto* item = gamesTable_->item(currentRow, 0);
    if (!item) return;

    constexpr int kDriveHeaderBgRole = Qt::UserRole + 1;
    if (item->data(kDriveHeaderBgRole).toBool()) {
        return;
    }

    const QString path = item->data(Qt::UserRole).toString();
    if (path.isEmpty()) return;

    bool exists = QDir(path).exists();
    if (exists) {
        const std::string normalizedPath = gsm::system::normalizePath(path.toStdString());
        const char* localAppData = std::getenv("LOCALAPPDATA");
        const std::string storageRoot = localAppData
            ? gsm::system::joinPath(localAppData, "GameStorageManager/metadata")
            : "metadata";

        gsm::core::SafetyMetadataStore store(storageRoot);
        const auto metadata = store.loadById(gsm::core::SafetyMetadataStore::makeStableId(normalizedPath));

        if (metadata.has_value() && metadata->state == gsm::core::SafetyOperationState::Completed) {
            QMessageBox::warning(this, "Cannot remove", "This game is currently optimized. You must Restore it before removing it from the library to prevent leaving compressed files behind.");
            return;
        }
    }

    auto it = std::remove_if(libraryGames_.begin(), libraryGames_.end(), [&](const gsm::core::GameEntry& g) {
        return QString::fromStdString(g.installPath).compare(path, Qt::CaseInsensitive) == 0;
    });

    if (it != libraryGames_.end()) {
        libraryGames_.erase(it, libraryGames_.end());
        saveLibrary();
        refreshTableView();
        applyStoredMetadata();
    }
}

void MainWindow::setBusy(bool busy)
{
    selectFolderButton_->setEnabled(!busy);
    scanSteamButton_->setEnabled(!busy);
    analyzeSelectedButton_->setEnabled(!busy);
    analyzeAllButton_->setEnabled(!busy);
    settingsButton_->setEnabled(!busy);
    if (busy) {
        optimizeButton_->setEnabled(false);
        restoreButton_->setEnabled(false);
    }
    cancelButton_->setEnabled(busy);
    removeButton_->setEnabled(!busy);
    profileCombo_->setEnabled(!busy);
    progressBar_->setRange(busy ? 0 : 0, busy ? 0 : 1);
    progressBar_->setValue(busy ? 0 : 1);
}

void MainWindow::onSettings()
{
    SettingsDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        int index = dialog.defaultAlgorithmIndex();
        profileCombo_->setCurrentIndex(index);
        onProfileChanged(index);
    }
}

} // namespace gsm::ui
