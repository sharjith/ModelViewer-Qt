#include "Logger.h"
#include "ConsolePanel.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>
#include <QSettings>
#include <iostream>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#endif

// Static instance
static Logger* g_loggerInstance = nullptr;

// Thread-local guard to prevent recursive logging during message processing
thread_local bool g_isLoggingMessage = false;

void Logger::qtMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    Logger::LogLevel level;

    switch (type)
    {
    case QtDebugMsg:
        level = Logger::Debug;
        break;
    case QtInfoMsg:
        level = Logger::Info;
        break;
    case QtWarningMsg:
        level = Logger::Warning;
        break;
    case QtCriticalMsg:
    case QtFatalMsg:
        level = Logger::Error;
        break;
    default:
        level = Logger::Debug;
        break;
    }

    // Extract function name from context if available
    QString contextInfo;
    if (context.function)
    {
        contextInfo = QString::fromLatin1(context.function);
    }

    Logger::instance().log(level, msg, contextInfo);
}

// ============================================================================
// LoggerStreamBuffer Implementation
// ============================================================================

LoggerStreamBuffer::LoggerStreamBuffer(Logger& logger, bool isError)
    : logger(logger), isError(isError)
{
}

LoggerStreamBuffer::~LoggerStreamBuffer()
{
}

int LoggerStreamBuffer::overflow(int c)
{
    if (c != EOF)
    {
        buffer += static_cast<char>(c);
        if (c == '\n')
        {
            sync();
        }
    }
    return c;
}

int LoggerStreamBuffer::sync()
{
    // Don't log if we're currently processing messages (prevent recursion)
    if (g_isLoggingMessage)
    {
        buffer.clear();
        return 0;
    }

    if (!buffer.empty())
    {
        // Remove trailing newline if present
        if (buffer.back() == '\n')
        {
            buffer.pop_back();
        }

        // Only log non-empty messages
        if (!buffer.empty())
        {
            QString msg = QString::fromStdString(buffer);
            Logger::LogLevel level = isError ? Logger::Error : Logger::Info;
            logger.log(level, msg, isError ? "std::cerr" : "std::cout");
        }
        buffer.clear();
    }
    return 0;
}

Logger& Logger::instance()
{
    if (!g_loggerInstance)
    {
        g_loggerInstance = new Logger();
    }
    return *g_loggerInstance;
}

QString Logger::getLogDirectory() const
{
    QString appDataLocation = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return appDataLocation + "/logs";
}

Logger::Logger()
    : workerThread(nullptr)
    , isRunning(false)
    , currentFileSuffix(0)
    , maxFileSize(10 * 1024 * 1024)  // 10 MB default
    , currentFileSize(0)
    , consoleEnabled(true)
    , fileEnabled(true)
    , minimumLevel(Debug)
    , coutBuffer(nullptr)
    , cerrBuffer(nullptr)
    , oldCoutBuffer(nullptr)
    , oldCerrBuffer(nullptr)
    , processingPending(false)
{
}

Logger::~Logger()
{
    shutdown();
}

void Logger::initialize(qint64 maxFileSizeBytes)
{
    if (isRunning)
    {
        return;  // Already initialized
    }

    maxFileSize = maxFileSizeBytes;
    baseTimestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss");
    currentFileSuffix = 0;
    currentFileSize = 0;

    ensureLogDirectoryExists();
    loadSettings();
    openLogFile();

    // Install Qt message handler to capture qDebug, qWarning, qCritical
    qInstallMessageHandler(qtMessageHandler);

    // Redirect std::cout and std::cerr to logger
    oldCoutBuffer = std::cout.rdbuf();
    oldCerrBuffer = std::cerr.rdbuf();

    coutBuffer = new LoggerStreamBuffer(*this, false);
    cerrBuffer = new LoggerStreamBuffer(*this, true);

    std::cout.rdbuf(coutBuffer);
    std::cerr.rdbuf(cerrBuffer);

    // Start worker thread for async file I/O
    isRunning = true;
    workerThread = new QThread();
    this->moveToThread(workerThread);

    connect(workerThread, &QThread::started, this, &Logger::processQueue);
    connect(this, &QObject::destroyed, workerThread, &QThread::quit);

    workerThread->start();

    // Console panel construction is deferred until notifyApplicationVisible()
    // fires from main.cpp - see that method's doc comment for why spawning it
    // here, before MainWindow exists, is exactly what caused the taskbar icon
    // race this was changed to avoid.
}

void Logger::shutdown()
{
    if (!isRunning)
    {
        return;
    }

    isRunning = false;

    // Destroy the console panel if it was ever created. Called on the main
    // thread (see main.cpp, right after app.exec() returns), so it's safe
    // to delete a QWidget directly here. hide() first (rather than just
    // deleting a still-visible widget outright) so ConsolePanel::hideEvent()
    // gets a chance to persist its geometry - if the app is closed while
    // the console is still open, a bare delete() never fires that event at
    // all, silently skipping the save for that session.
    if (consolePanel)
    {
        consolePanel->hide();
        delete consolePanel;
    }

    // Restore original stream buffers
    if (oldCoutBuffer)
    {
        std::cout.rdbuf(oldCoutBuffer);
    }
    if (oldCerrBuffer)
    {
        std::cerr.rdbuf(oldCerrBuffer);
    }

    delete coutBuffer;
    delete cerrBuffer;
    coutBuffer = nullptr;
    cerrBuffer = nullptr;

    if (workerThread)
    {
        // Process any remaining messages
        processQueue();

        workerThread->quit();
        workerThread->wait();
        workerThread->deleteLater();
        workerThread = nullptr;
    }

    if (currentLogFile.isOpen())
    {
        currentLogFile.close();
    }
}

void Logger::debug(const QString& message, const QString& context)
{
    log(Debug, message, context);
}

void Logger::info(const QString& message, const QString& context)
{
    log(Info, message, context);
}

void Logger::warning(const QString& message, const QString& context)
{
    log(Warning, message, context);
}

void Logger::error(const QString& message, const QString& context)
{
    log(Error, message, context);
}

void Logger::log(LogLevel level, const QString& message, const QString& context)
{
    // Prevent recursive logging during message processing
    if (g_isLoggingMessage)
    {
        return;
    }

    if (level < minimumLevel)
    {
        return;  // Below minimum level, skip
    }

    LogMessage msg;
    msg.level = level;
    msg.message = message;
    msg.context = context;
    msg.timestamp = QDateTime::currentDateTime();

    bool shouldInvokeProcessing = false;
    {
        QMutexLocker locker(&queueMutex);
        messageQueue.enqueue(msg);
    }

    // Only trigger processing if it's not already pending
    if (workerThread && isRunning)
    {
        QMutexLocker locker(&processingMutex);
        if (!processingPending)
        {
            processingPending = true;
            shouldInvokeProcessing = true;
        }
    }

    if (shouldInvokeProcessing)
    {
        QMetaObject::invokeMethod(this, &Logger::processQueue, Qt::QueuedConnection);
    }
}

void Logger::processQueue()
{
    // Set flag to prevent recursive logging during output
    g_isLoggingMessage = true;

    // Process all messages currently in queue
    while (true)
    {
        LogMessage msg;

        {
            QMutexLocker locker(&queueMutex);
            if (messageQueue.isEmpty())
            {
                break;
            }
            msg = messageQueue.dequeue();
        }

        QString formatted = formatLogMessage(msg.level, msg.message, msg.context);

        // Original-stdout output - only reaches anywhere visible if the
        // process actually inherited a real terminal (e.g. launched from a
        // shell on Linux, or from a parent console via AttachConsole
        // elsewhere); harmless no-op otherwise.
        if (consoleEnabled && oldCoutBuffer)
        {
            QByteArray ba = formatted.toUtf8();
            oldCoutBuffer->sputn(ba.constData(), ba.length());
            oldCoutBuffer->sputc('\n');
            oldCoutBuffer->pubsync();
        }

        // Console panel output - this runs on Logger's own worker thread
        // (see moveToThread() in initialize()), but consolePanel lives on
        // the main GUI thread, so the append has to be marshalled over via
        // a queued call rather than touched directly.
        if (consoleEnabled && consolePanel)
        {
            QMetaObject::invokeMethod(consolePanel, "appendLine",
                Qt::QueuedConnection, Q_ARG(QString, formatted));
        }

#ifdef _WIN32
        // ModelViewer is WIN32_EXECUTABLE (see CMakeLists.txt) - it has no
        // console/stdout attached unless spawnConsole() pops one open, so on
        // Linux (where the IDE's output pane just captures the process's
        // real, inherited stdout) these messages show up "for free", but on
        // Windows they'd otherwise only ever reach the log file or that
        // separate console window. OutputDebugStringW is what IDE debug/
        // output panes (Visual Studio's Output window, Qt Creator's
        // Application Output under the MSVC debugger) actually capture for a
        // GUI-subsystem app - it's a no-op with no measurable cost when no
        // debugger is attached, so this isn't gated behind IsDebuggerPresent().
        OutputDebugStringW((formatted + QStringLiteral("\n")).toStdWString().c_str());
#endif

        // File output
        if (fileEnabled && currentLogFile.isOpen())
        {
            rotateLogFileIfNeeded();
            writeToFile(formatted);
        }
    }

    // Clear the flag before clearing pending
    g_isLoggingMessage = false;

    // Clear the pending flag so future logs can trigger processing again
    {
        QMutexLocker locker(&processingMutex);
        processingPending = false;
    }

    // If new messages were added while we were processing, trigger another round
    {
        QMutexLocker locker(&queueMutex);
        if (!messageQueue.isEmpty())
        {
            QMutexLocker procLocker(&processingMutex);
            if (!processingPending)
            {
                processingPending = true;
                QMetaObject::invokeMethod(this, &Logger::processQueue, Qt::QueuedConnection);
            }
        }
    }
}

QString Logger::formatLogMessage(LogLevel level, const QString& message, const QString& context) const
{
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    QString levelStr = levelToString(level);

    if (context.isEmpty())
    {
        return QString("[%1] %2 | %3").arg(timestamp, levelStr, message);
    }
    else
    {
        return QString("[%1] %2 | %3 | %4").arg(timestamp, levelStr, context, message);
    }
}

QString Logger::levelToString(LogLevel level) const
{
    switch (level)
    {
    case Debug:
        return "DEBUG";
    case Info:
        return "INFO ";
    case Warning:
        return "WARN ";
    case Error:
        return "ERROR";
    default:
        return "UNKN ";
    }
}

void Logger::ensureLogDirectoryExists()
{
    QDir dir(getLogDirectory());
    if (!dir.exists())
    {
        dir.mkpath(".");
    }
}

QString Logger::generateLogFilename(int suffix) const
{
    if (suffix < 0)
    {
        return QString("modelviewer_%1.log").arg(baseTimestamp);
    }
    else
    {
        return QString("modelviewer_%1_%2.log").arg(baseTimestamp).arg(suffix, 3, 10, QChar('0'));
    }
}

bool Logger::openLogFile()
{
    currentLogFilePath = getLogDirectory() + "/" + generateLogFilename(currentFileSuffix < 1 ? -1 : currentFileSuffix - 1);
    currentLogFile.setFileName(currentLogFilePath);

    if (!currentLogFile.open(QIODevice::Append | QIODevice::Text))
    {
        std::cerr << "Failed to open log file: " << currentLogFilePath.toStdString() << std::endl;
        return false;
    }

    currentFileSize = currentLogFile.size();
    return true;
}

bool Logger::writeToFile(const QString& formattedMessage)
{
    if (!currentLogFile.isOpen())
    {
        return false;
    }

    QByteArray data = (formattedMessage + "\n").toUtf8();
    qint64 bytesWritten = currentLogFile.write(data);
    currentLogFile.flush();

    currentFileSize += bytesWritten;
    return bytesWritten == data.size();
}

void Logger::rotateLogFileIfNeeded()
{
    if (currentFileSize < maxFileSize)
    {
        return;
    }

    // Close current file
    if (currentLogFile.isOpen())
    {
        currentLogFile.close();
    }

    // Open next rotated file
    currentFileSuffix++;
    if (!openLogFile())
    {
        std::cerr << "Failed to rotate log file" << std::endl;
    }
}

void Logger::loadSettings()
{
    QSettings settings;

    consoleEnabled = settings.value("logging/consoleEnabled", true).toBool();
    fileEnabled = settings.value("logging/fileEnabled", true).toBool();

    int levelValue = settings.value("logging/minimumLevel", static_cast<int>(Debug)).toInt();
    minimumLevel = static_cast<LogLevel>(levelValue);

    consoleBufferLines = settings.value("logging/consoleBufferLines", 20000).toInt();
}

void Logger::setConsoleEnabled(bool enabled)
{
    consoleEnabled = enabled;
    QSettings settings;
    settings.setValue("logging/consoleEnabled", enabled);

    if (enabled)
    {
        // Before the app is fully visible, defer actually constructing the
        // panel to notifyApplicationVisible() - this can otherwise be called
        // very early (main.cpp at startup, before MainWindow exists) or later
        // (Settings dialog toggling the checkbox while already running); only
        // the former needs to wait.
        if (applicationVisible)
        {
            spawnConsole();
        }
    }
    else if (consolePanel)
    {
        consolePanel->hide();
    }
}

void Logger::spawnConsole(bool stealFocus)
{
    // Called synchronously from the main GUI thread in every caller
    // (notifyApplicationVisible() at startup, SettingsDialog when the
    // checkbox is applied), so constructing a QWidget here is safe. See
    // ConsolePanel.h for why this is a plain Qt window instead of a real OS
    // console.
    if (!consolePanel)
    {
        consolePanel = new ConsolePanel();
        consolePanel->setMaxLineCount(consoleBufferLines);
    }
    consolePanel->show();
    if (stealFocus)
    {
        consolePanel->raise();
        // raise() alone only reorders Z-order - it doesn't transfer window
        // activation. Without an explicit activate, Windows can leave
        // MainWindow (the previously-active window) stuck showing as
        // deactivated and not receiving input, without the console cleanly
        // taking activation either - neither window responds until something
        // else forces a real activation change (e.g. clicking the console).
        // Only done for an explicit user request to see the console - NOT
        // for the silent, automatic spawn at startup (see
        // notifyApplicationVisible()), where stealing focus from the
        // just-shown MainWindow left the console, not MainWindow, as the
        // OS's "last active" window for this app - which in turn broke
        // MainWindow::closeEvent()'s AttachThreadInput foreground-forcing
        // the next time an external "Close all windows" request arrived.
        consolePanel->activateWindow();
    }
    else
    {
        // Silent/automatic startup spawn - the console isn't meant to grab
        // attention here, so push it behind MainWindow (which is already
        // shown and active by this point) instead of popping up on top of
        // the app the user just launched. lower() is the z-order-only
        // counterpart to raise() above - same native window-stacking
        // mechanism, no activation change either way.
        consolePanel->lower();
    }
}

void Logger::setFileEnabled(bool enabled)
{
    fileEnabled = enabled;
    QSettings settings;
    settings.setValue("logging/fileEnabled", enabled);
}

void Logger::setMinimumLevel(LogLevel level)
{
    minimumLevel = level;
    QSettings settings;
    settings.setValue("logging/minimumLevel", static_cast<int>(level));
}

void Logger::setConsoleWindowVisible(bool visible)
{
    if (consolePanel)
    {
        consolePanel->setVisible(visible);
    }
}

void Logger::setConsoleBufferLines(int lines)
{
    consoleBufferLines = lines;
    QSettings settings;
    settings.setValue("logging/consoleBufferLines", lines);

    if (consolePanel)
    {
        consolePanel->setMaxLineCount(lines);
    }
}

void Logger::notifyApplicationVisible()
{
    if (applicationVisible)
    {
        return;
    }
    applicationVisible = true;

    if (consoleEnabled)
    {
        // false: this is the silent, automatic startup spawn - see
        // spawnConsole()'s stealFocus doc comment for why it must not steal
        // OS-level activation away from MainWindow here.
        spawnConsole(false);
    }
}
