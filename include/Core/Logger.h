#pragma once

#include <QObject>
#include <QString>
#include <QQueue>
#include <QMutex>
#include <QFile>
#include <QThread>
#include <QDateTime>
#include <QPointer>
#include <sstream>
#include <iostream>

#include <sstream>
#include <iostream>

class ConsolePanel;

/**
 * @class LoggerStreamBuffer
 * @brief Custom stream buffer that redirects std::cout/std::cerr to Logger
 *
 * Replaces the stream buffer of std::cout and std::cerr, capturing all output
 * and forwarding it to the Logger class.
 */
class LoggerStreamBuffer : public std::streambuf
{
public:
    explicit LoggerStreamBuffer(class Logger& logger, bool isError = false);
    ~LoggerStreamBuffer();

protected:
    int overflow(int c) override;
    int sync() override;

private:
    Logger& logger;
    bool isError;
    std::string buffer;
};

/**
 * @class Logger
 * @brief Thread-safe logger for ModelViewer with console and file output.
 *
 * Provides unified logging interface replacing qDebug and std::cout.
 * Supports independent console and file output with configurable verbosity.
 * File output uses async worker thread to avoid blocking render thread.
 * Automatic size-based file rotation with date+time based filenames.
 *
 * Qt message handler integration: Automatically captures qDebug, qWarning,
 * qCritical output without requiring code changes.
 */
class Logger : public QObject
{
    Q_OBJECT

public:
    enum LogLevel
    {
        Debug = 0,
        Info,
        Warning,
        Error
    };

    /**
     * @brief Get singleton instance
     */
    static Logger& instance();

    /**
     * @brief Qt message handler - installed automatically by initialize()
     * Redirects qDebug, qWarning, qCritical, qFatal to logger
     */
    static void qtMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg);

    /**
     * @brief Initialize logger with settings
     * @param maxFileSizeBytes Maximum size per log file before rotation (default 10 MB)
     *
     * Automatically installs Qt message handler to capture qDebug, qWarning, qCritical output.
     * Also redirects std::cout and std::cerr to logger.
     */
    void initialize(qint64 maxFileSizeBytes = 10 * 1024 * 1024);

    /**
     * @brief Shutdown logger and flush all pending messages
     */
    void shutdown();

    // Logging API
    void debug(const QString& message, const QString& context = "");
    void info(const QString& message, const QString& context = "");
    void warning(const QString& message, const QString& context = "");
    void error(const QString& message, const QString& context = "");

    /**
     * @brief Internal log function (used by qt message handler and direct calls)
     */
    void log(LogLevel level, const QString& message, const QString& context = "");

    /**
     * @brief Load settings from configuration
     */
    void loadSettings();

    /**
     * @brief Set console output enabled/disabled
     */
    void setConsoleEnabled(bool enabled);

    /**
     * @brief Set file output enabled/disabled
     */
    void setFileEnabled(bool enabled);

    /**
     * @brief Set minimum log level to output
     */
    void setMinimumLevel(LogLevel level);

    /**
     * @brief Show or hide the console panel window
     */
    void setConsoleWindowVisible(bool visible);

    /**
     * @brief Set the console panel's scrollback line limit
     */
    void setConsoleBufferLines(int lines);

    /**
     * @brief Get current log directory path
     */
    QString getLogDirectory() const;

    /**
     * @brief Notify the logger that the main window has fully appeared.
     *
     * Call once from main.cpp right after the main window is shown. If the
     * console is enabled, ConsolePanel's actual construction is deferred
     * until this fires - constructing it (and its native WM_SETICON call)
     * during early startup, before MainWindow exists and has established
     * its own taskbar identity, raced Explorer's taskbar icon assignment
     * and intermittently left the app showing the generic EXE icon instead
     * of its own.
     */
    void notifyApplicationVisible();

private:
    // Private constructor for singleton
    Logger();
    ~Logger();

    // Prevent copy/move
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // Internal logging
    void flushQueue(); // Background worker slot
    QString levelToString(LogLevel level) const;
    QString formatLogMessage(LogLevel level, const QString& message, const QString& context) const;

    // File handling    
    QString generateLogFilename(int suffix = -1) const;
    bool openLogFile();
    bool writeToFile(const QString& formattedMessage);
    void rotateLogFileIfNeeded();
    void ensureLogDirectoryExists();

    // Console panel management (a Qt widget that mimics a console window,
    // rather than a real OS console - see ConsolePanel.h for why).
    // stealFocus forces OS-level activation onto the console (wanted when
    // the user explicitly asks to see it - Settings checkbox, Show Console
    // menu action) - pass false for an automatic/silent spawn (the deferred
    // startup call from notifyApplicationVisible()) so it doesn't steal
    // foreground activation away from MainWindow.
    void spawnConsole(bool stealFocus = true);

    // Worker thread slots
private slots:
    void processQueue();

private:
    // Message queue for async logging
    struct LogMessage
    {
        LogLevel level;
        QString message;
        QString context;
        QDateTime timestamp;
    };

    QQueue<LogMessage> messageQueue;
    QMutex queueMutex;

    // Worker thread for file I/O
    QThread* workerThread;
    bool isRunning;

    // File output management
    QFile currentLogFile;
    QString currentLogFilePath;
    QString baseTimestamp;      // e.g., "2025-01-21_14-30-45"
    int currentFileSuffix;      // 0 for initial, 1+ for rotated files
    qint64 maxFileSize;
    qint64 currentFileSize;

    // Settings
    bool consoleEnabled;
    bool fileEnabled;
    LogLevel minimumLevel;
    int consoleBufferLines = 20000;

    // Set once by notifyApplicationVisible() - see its doc comment. Gates
    // spawnConsole() so ConsolePanel is never constructed before that point.
    bool applicationVisible = false;

    // Stream redirection
    LoggerStreamBuffer* coutBuffer;
    LoggerStreamBuffer* cerrBuffer;
    std::streambuf* oldCoutBuffer;
    std::streambuf* oldCerrBuffer;

    // Processing flag to prevent redundant invocations
    bool processingPending;
    QMutex processingMutex;

    // Console panel - lazily created on the main thread; QPointer so it
    // safely nulls out if ever destroyed some other way
    QPointer<ConsolePanel> consolePanel;
};
