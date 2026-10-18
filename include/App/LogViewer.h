#pragma once

#include <QDialog>
#include <QTimer>
#include <memory>

namespace Ui
{
    class LogViewer;
}

class LogHighlighter;

/**
 * @class LogViewer
 * @brief Non-modal dialog for viewing and searching application logs
 *
 * Features:
 * - File list with auto-refresh
 * - Content viewer with syntax highlighting
 * - Real-time search with navigation
 * - Log level filtering
 * - Settings persistence
 */
class LogViewer : public QDialog
{
    Q_OBJECT

public:
    explicit LogViewer(QWidget* parent = nullptr);
    ~LogViewer();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    // File list
    void onFileListItemChanged(int row);
    void onRefreshFileList();

    // Search
    void onSearchTextChanged(const QString& text);
    void onSearchPrevious();
    void onSearchNext();
    void onCaseSensitiveToggled(bool checked);

    // Filtering
    void onDebugFilterToggled(bool checked);
    void onInfoFilterToggled(bool checked);
    void onWarningFilterToggled(bool checked);
    void onErrorFilterToggled(bool checked);

    // Content
    void onContentChanged();

private:
    // Initialization
    void setupConnections();
    void configureUI();

    // File Operations
    void loadLogDirectory();
    void loadLogFile(const QString& filePath);
    QString getLogDirectory() const;

    // Search Operations
    void performSearch();
    void navigateToMatch(int direction);
    void highlightMatches();
    void updateSearchCounter();

    // Filtering
    void applyFilters();
    QString filterLogContent(const QString& content);
    bool shouldShowLine(const QString& line) const;

    // Settings
    void loadSettings();
    void saveSettings();

    // State
    QString currentFilePath;
    QString originalContent;  // Unfiltered content
    QString filteredContent;  // After filtering

    QStringList searchMatches;
    int currentMatchIndex;

    bool isDebugEnabled;
    bool isInfoEnabled;
    bool isWarningEnabled;
    bool isErrorEnabled;

    // Auto-refresh
    QTimer* fileListRefreshTimer;

    // Syntax highlighting
    std::unique_ptr<LogHighlighter> highlighter;

    // UI
    std::unique_ptr<Ui::LogViewer> ui;

    // Constants
    static constexpr int FILE_LIST_REFRESH_INTERVAL = 5000;  // 5 seconds
};
