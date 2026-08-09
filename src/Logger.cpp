#include "Logger.h"

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
    , consoleAllocated(false)
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

    // If console was enabled in settings, spawn it on startup
    if (consoleEnabled)
    {
        spawnConsole();
    }
}

void Logger::shutdown()
{
    if (!isRunning)
    {
        return;
    }

    isRunning = false;

    // Free console if allocated
#ifdef _WIN32
    if (consoleAllocated)
    {
        HWND consoleWindow = GetConsoleWindow();
        if (consoleWindow)
        {
            ShowWindow(consoleWindow, SW_HIDE);
        }
        FreeConsole();
        consoleAllocated = false;
    }
#endif

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

        // Console output - use original stdout buffer to avoid feedback loop
        if (consoleEnabled && oldCoutBuffer)
        {
            QByteArray ba = formatted.toUtf8();
            oldCoutBuffer->sputn(ba.constData(), ba.length());
            oldCoutBuffer->sputc('\n');
            oldCoutBuffer->pubsync();
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
}

void Logger::setConsoleEnabled(bool enabled)
{
    consoleEnabled = enabled;
    QSettings settings;
    settings.setValue("logging/consoleEnabled", enabled);

#ifdef _WIN32
    if (enabled)
    {
        // Show console - allocate if not already allocated, or show if hidden
        if (!consoleAllocated)
        {
            spawnConsole();
        }
        else
        {
            // Console already allocated, just show the hidden window
            HWND consoleWindow = GetConsoleWindow();
            if (consoleWindow)
            {
                ShowWindow(consoleWindow, SW_SHOW);
            }
        }
    }
    else
    {
        // Hide console window
        if (consoleAllocated)
        {
            HWND consoleWindow = GetConsoleWindow();
            if (consoleWindow)
            {
                ShowWindow(consoleWindow, SW_HIDE);
            }
        }
    }
#endif
}

void Logger::spawnConsole()
{
#ifdef _WIN32
    if (consoleAllocated)
    {
        return;  // Already allocated
    }

    // Try to allocate a new console window
    if (AllocConsole() || AttachConsole(ATTACH_PARENT_PROCESS))
    {
        // Redirect stdout to the console
        FILE* file = nullptr;
        freopen_s(&file, "CONOUT$", "w", stdout);
        freopen_s(&file, "CONOUT$", "w", stderr);
        freopen_s(&file, "CONIN$", "r", stdin);
        if (file) fclose(file);

        // Make the console window visible
        HWND consoleWindow = GetConsoleWindow();
        if (consoleWindow)
        {
            ShowWindow(consoleWindow, SW_SHOW);
        }

        consoleAllocated = true;
    }
#endif
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
#ifdef _WIN32
    HWND consoleWindow = GetConsoleWindow();
    if (consoleWindow)
    {
        ShowWindow(consoleWindow, visible ? SW_SHOW : SW_HIDE);
    }
#endif
}
