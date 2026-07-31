#include "LogViewer.h"
#include "ui_LogViewer.h"
#include "LogHighlighter.h"
#include "LanguageManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QStatusBar>
#include <QSplitter>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QTimer>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextCursor>
#include <QFont>
#include <QFile>
#include <QCloseEvent>

LogViewer::LogViewer(QWidget* parent)
    : QDialog(parent)
    , ui(std::make_unique<Ui::LogViewer>())
    , currentMatchIndex(0)
    , isDebugEnabled(true)
    , isInfoEnabled(true)
    , isWarningEnabled(true)
    , isErrorEnabled(true)
    , fileListRefreshTimer(nullptr)
{

    ui->setupUi(this);

    // See ExplodedViewPanel's/ClippingPlanesEditor's identical connection -
    // without this, a live language switch in Settings left this dialog
    // showing whatever language was active at construction until the next
    // app restart.
    connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
        ui->retranslateUi(this);
        });

    configureUI();
    setupConnections();
    loadSettings();
    loadLogDirectory();

    // Set up auto-refresh for file list
    fileListRefreshTimer = new QTimer(this);
    connect(fileListRefreshTimer, &QTimer::timeout, this, &LogViewer::onRefreshFileList);
    fileListRefreshTimer->start(FILE_LIST_REFRESH_INTERVAL);

    // Apply syntax highlighting
    highlighter = std::make_unique<LogHighlighter>(ui->contentViewer->document());
}

LogViewer::~LogViewer()
{
    saveSettings();
}

void LogViewer::configureUI()
{
    // Configure content viewer font
    QFont font("Courier");
    font.setPointSize(9);
    ui->contentViewer->setFont(font);

    // Set up splitter proportions
    ui->mainSplitter->setSizes({ 300, 900 });
    ui->mainSplitter->setCollapsible(0, false);
    ui->mainSplitter->setCollapsible(1, false);

    // Initialize status bar
    ui->statusBar->showMessage("Ready");
}

void LogViewer::setupConnections()
{
    // File list
    connect(ui->fileListWidget, QOverload<int>::of(&QListWidget::currentRowChanged),
        this, &LogViewer::onFileListItemChanged);

    // Search
    connect(ui->searchInput, &QLineEdit::textChanged,
        this, &LogViewer::onSearchTextChanged);
    connect(ui->searchPrevButton, &QPushButton::clicked,
        this, &LogViewer::onSearchPrevious);
    connect(ui->searchNextButton, &QPushButton::clicked,
        this, &LogViewer::onSearchNext);
    connect(ui->caseSensitiveCheckbox, &QCheckBox::toggled,
        this, &LogViewer::onCaseSensitiveToggled);

    // Filtering
    connect(ui->debugCheckbox, &QCheckBox::toggled,
        this, &LogViewer::onDebugFilterToggled);
    connect(ui->infoCheckbox, &QCheckBox::toggled,
        this, &LogViewer::onInfoFilterToggled);
    connect(ui->warningCheckbox, &QCheckBox::toggled,
        this, &LogViewer::onWarningFilterToggled);
    connect(ui->errorCheckbox, &QCheckBox::toggled,
        this, &LogViewer::onErrorFilterToggled);

    // Content
    connect(ui->contentViewer, &QPlainTextEdit::textChanged,
        this, &LogViewer::onContentChanged);
}

void LogViewer::loadLogDirectory()
{
    QString logDir = getLogDirectory();
    QDir dir(logDir);

    QStringList filters;
    filters << "modelviewer_*.log";

    QFileInfoList files = dir.entryInfoList(filters, QDir::Files, QDir::Time);

    // Only update if the file list actually changed
    QStringList currentFiles;
    for (int i = 0; i < ui->fileListWidget->count(); ++i)
    {
        currentFiles.append(ui->fileListWidget->item(i)->data(Qt::UserRole).toString());
    }

    // Build new file list
    QStringList newFiles;
    for (const QFileInfo& fileInfo : files)
    {
        newFiles.append(fileInfo.absoluteFilePath());
    }

    // If files haven't changed, don't update
    if (currentFiles == newFiles)
    {
        return;
    }

    ui->fileListWidget->clear();

    for (const QFileInfo& fileInfo : files)
    {
        QString displayName = QString("%1 (%2 KB)")
            .arg(fileInfo.fileName())
            .arg(fileInfo.size() / 1024);

        QListWidgetItem* item = new QListWidgetItem(displayName);
        item->setData(Qt::UserRole, fileInfo.absoluteFilePath());

        ui->fileListWidget->addItem(item);
    }

    // Select most recent file only if nothing was selected
    if (ui->fileListWidget->count() > 0 && currentFilePath.isEmpty())
    {
        ui->fileListWidget->setCurrentRow(0);
    }
}

void LogViewer::onFileListItemChanged(int row)
{
    if (row < 0)
    {
        ui->contentViewer->clear();
        ui->statusBar->showMessage("No log file selected");
        currentFilePath.clear();
        return;
    }

    QListWidgetItem* item = ui->fileListWidget->item(row);
    if (!item) return;

    QString filePath = item->data(Qt::UserRole).toString();

    // Only load if it's a different file
    if (filePath == currentFilePath)
    {
        return;
    }

    loadLogFile(filePath);
}

void LogViewer::loadLogFile(const QString& filePath)
{
    currentFilePath = filePath;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        ui->statusBar->showMessage("Failed to open: " + filePath);
        return;
    }

    originalContent = QString::fromUtf8(file.readAll());
    file.close();

    // Apply filters
    applyFilters();

    // Clear search state
    searchMatches.clear();
    currentMatchIndex = 0;
    ui->searchInput->clear();

    // Update status
    QFileInfo fileInfo(filePath);
    int lineCount = originalContent.count('\n');
    ui->statusBar->showMessage(QString("File: %1 | Entries: %2")
        .arg(fileInfo.fileName())
        .arg(lineCount));
}

void LogViewer::applyFilters()
{
    filteredContent = filterLogContent(originalContent);
    ui->contentViewer->setPlainText(filteredContent);
}

QString LogViewer::filterLogContent(const QString& content)
{
    QStringList lines = content.split('\n');
    QStringList filtered;

    for (const QString& line : lines)
    {
        if (shouldShowLine(line))
        {
            filtered.append(line);
        }
    }

    return filtered.join('\n');
}

bool LogViewer::shouldShowLine(const QString& line) const
{
    if (line.isEmpty()) return true;

    // Parse log level from line
    // Format: [timestamp] LEVEL | ...
    QRegularExpression levelPattern(R"(\]\s+([A-Z]+)\s)");
    QRegularExpressionMatch match = levelPattern.match(line);

    if (!match.hasMatch())
    {
        return true;  // If we can't parse, show it
    }

    QString level = match.captured(1);

    if (level == "DEBUG")
    {
        return isDebugEnabled;
    }
    else if (level == "INFO")
    {
        return isInfoEnabled;
    }
    else if (level == "WARN")
    {
        return isWarningEnabled;
    }
    else if (level == "ERROR")
    {
        return isErrorEnabled;
    }

    return true;
}

void LogViewer::onDebugFilterToggled(bool checked)
{
    isDebugEnabled = checked;
    applyFilters();
    performSearch();
}

void LogViewer::onInfoFilterToggled(bool checked)
{
    isInfoEnabled = checked;
    applyFilters();
    performSearch();
}

void LogViewer::onWarningFilterToggled(bool checked)
{
    isWarningEnabled = checked;
    applyFilters();
    performSearch();
}

void LogViewer::onErrorFilterToggled(bool checked)
{
    isErrorEnabled = checked;
    applyFilters();
    performSearch();
}

void LogViewer::onSearchTextChanged(const QString& text)
{
    performSearch();
}

void LogViewer::performSearch()
{
    QString searchText = ui->searchInput->text();

    // Clear formatting
    QTextDocument* doc = ui->contentViewer->document();
    QTextCursor cursor(doc);
    cursor.select(QTextCursor::Document);
    cursor.setCharFormat(QTextCharFormat());

    searchMatches.clear();
    currentMatchIndex = 0;
    updateSearchCounter();

    if (searchText.isEmpty())
    {
        return;
    }

    highlightMatches();
}

void LogViewer::navigateToMatch(int direction)
{
    if (searchMatches.isEmpty()) return;

    if (direction > 0)
    {
        currentMatchIndex = (currentMatchIndex + 1) % searchMatches.size();
    }
    else if (direction < 0)
    {
        currentMatchIndex = (currentMatchIndex - 1 + searchMatches.size()) % searchMatches.size();
    }
    else
    {
        currentMatchIndex = 0;
    }

    // Find the current match and scroll to it
    QString searchText = ui->searchInput->text();
    if (searchText.isEmpty())
    {
        updateSearchCounter();
        return;
    }

    // Determine case sensitivity
    Qt::CaseSensitivity caseSensitivity = ui->caseSensitiveCheckbox->isChecked()
        ? Qt::CaseSensitive : Qt::CaseInsensitive;

    QTextDocument* doc = ui->contentViewer->document();
    QTextCursor cursor(doc);
    cursor.movePosition(QTextCursor::Start);

    int matchIndex = 0;
    QTextDocument::FindFlags flags;
    if (caseSensitivity == Qt::CaseSensitive)
    {
        flags = QTextDocument::FindCaseSensitively;
    }

    while (!cursor.isNull())
    {
        cursor = doc->find(searchText, cursor, flags);

        if (!cursor.isNull())
        {
            if (matchIndex == currentMatchIndex)
            {
                // Found the match we want to navigate to
                // Select it and scroll to it
                ui->contentViewer->setTextCursor(cursor);
                ui->contentViewer->ensureCursorVisible();
                break;
            }
            matchIndex++;
        }
    }

    updateSearchCounter();
}

void LogViewer::highlightMatches()
{
    QString searchText = ui->searchInput->text();
    if (searchText.isEmpty())
    {
        return;
    }

    QTextDocument* doc = ui->contentViewer->document();
    QTextCharFormat highlightFormat;
    highlightFormat.setBackground(QColor(255, 255, 0, 200));

    // Determine case sensitivity
    Qt::CaseSensitivity caseSensitivity = ui->caseSensitiveCheckbox->isChecked()
        ? Qt::CaseSensitive : Qt::CaseInsensitive;

    // Simple: find all and highlight
    QTextCursor cursor(doc);
    cursor.movePosition(QTextCursor::Start);

    int count = 0;
    QTextDocument::FindFlags flags;
    if (caseSensitivity == Qt::CaseSensitive)
    {
        flags = QTextDocument::FindCaseSensitively;
    }

    while (!cursor.isNull())
    {
        cursor = doc->find(searchText, cursor, flags);

        if (!cursor.isNull())
        {
            cursor.mergeCharFormat(highlightFormat);
            searchMatches.append("match");
            count++;
        }
    }

    updateSearchCounter();

    // Navigate to first match if any found
    if (!searchMatches.isEmpty())
    {
        currentMatchIndex = 0;
        navigateToMatch(0);
    }
}

void LogViewer::onSearchPrevious()
{
    navigateToMatch(-1);
}

void LogViewer::onSearchNext()
{
    navigateToMatch(1);
}

void LogViewer::onCaseSensitiveToggled(bool checked)
{
    performSearch();
}

void LogViewer::updateSearchCounter()
{
    int total = searchMatches.size();
    int current = (total > 0) ? (currentMatchIndex + 1) : 0;
    ui->searchCounterLabel->setText(QString("%1/%2").arg(current).arg(total));
}

void LogViewer::onRefreshFileList()
{
    loadLogDirectory();
}

void LogViewer::onContentChanged()
{
    // Placeholder for future enhancements
}

QString LogViewer::getLogDirectory() const
{
    QString appDataLocation = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return appDataLocation + "/logs";
}

void LogViewer::loadSettings()
{
    QSettings settings;

    // Window geometry (size and position)
    QByteArray geometry = settings.value("logviewer/geometry", QByteArray()).toByteArray();
    if (!geometry.isEmpty())
    {
        restoreGeometry(geometry);
    }

    // Filter states
    isDebugEnabled = settings.value("logviewer/showDebug", true).toBool();
    isInfoEnabled = settings.value("logviewer/showInfo", true).toBool();
    isWarningEnabled = settings.value("logviewer/showWarning", true).toBool();
    isErrorEnabled = settings.value("logviewer/showError", true).toBool();

    ui->debugCheckbox->setChecked(isDebugEnabled);
    ui->infoCheckbox->setChecked(isInfoEnabled);
    ui->warningCheckbox->setChecked(isWarningEnabled);
    ui->errorCheckbox->setChecked(isErrorEnabled);
}

void LogViewer::saveSettings()
{
    QSettings settings;

    // Window geometry (size and position)
    settings.setValue("logviewer/geometry", saveGeometry());

    // Filter states
    settings.setValue("logviewer/showDebug", isDebugEnabled);
    settings.setValue("logviewer/showInfo", isInfoEnabled);
    settings.setValue("logviewer/showWarning", isWarningEnabled);
    settings.setValue("logviewer/showError", isErrorEnabled);
}

void LogViewer::closeEvent(QCloseEvent* event)
{
    saveSettings();
    QDialog::closeEvent(event);
}
