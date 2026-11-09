// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#include "ImportDiagnostics.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <mutex>

#ifdef Q_OS_WIN
#  include <qt_windows.h>
#  include <psapi.h>
#  include <io.h>
#else
#  include <unistd.h>
#endif

namespace {
void terminated() noexcept;
struct Trace
{
    std::mutex mutex;
    FILE* file = nullptr;
    std::terminate_handler previous = nullptr;
    quint64 sequence = 0;

    ~Trace()
    {
        if (!file)
            return;
        if (std::get_terminate() == terminated)
            std::set_terminate(previous);
        std::fclose(file);
    }
};

Trace& trace()
{
    static Trace state;
    return state;
}

void write(const char* stage, const char* reason, qint64 index,
           quint64 done, quint64 total) noexcept
{
    try
    {
        auto& state = trace();
        std::lock_guard<std::mutex> lock(state.mutex);
        if (!state.file)
            return;

        quint64 privateBytes = 0;
        quint64 workingSet = 0;
        bool memoryAvailable = false;
#ifdef Q_OS_WIN
        using Query = BOOL (WINAPI*)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
        static const auto query = reinterpret_cast<Query>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32GetProcessMemoryInfo"));
        PROCESS_MEMORY_COUNTERS_EX counters{};
        counters.cb = sizeof(counters);
        if (query && query(GetCurrentProcess(),
                           reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&counters),
                           sizeof(counters)))
        {
            privateBytes = counters.PrivateUsage;
            workingSet = counters.WorkingSetSize;
            memoryAvailable = true;
        }
#endif
        // Keep disk usage bounded while retaining the most recent checkpoints.
        // MODELVIEWER_IMPORT_DIAGNOSTICS_MAX_MB (or the "importDiagnosticsMaxMB" setting) raises the cap (default 32 MB);
        // a 30000-part import writes far more than that.
        static const long long maxBytes = [] {
            bool ok = false;
            const long long mb = qEnvironmentVariableIntValue("MODELVIEWER_IMPORT_DIAGNOSTICS_MAX_MB", &ok);
            if (ok && mb >= 1)
                return mb * 1024 * 1024;
            // Settings dialog value (Debug tab); read once per run like the environment variable.
            return qMax<long long>(1, QSettings().value(QStringLiteral("importDiagnosticsMaxMB"), 32).toLongLong()) * 1024 * 1024;
        }();
        if (std::ftell(state.file) > maxBytes)
        {
#ifdef Q_OS_WIN
            const bool truncated = _chsize_s(_fileno(state.file), 0) == 0;
#else
            const bool truncated = ftruncate(fileno(state.file), 0) == 0;
#endif
            if (truncated)
            {
                std::rewind(state.file);
                std::fputs("TRACE WRAPPED: most recent checkpoints follow\n", state.file);
            }
        }

        std::fprintf(
            state.file,
            "seq=%llu timeMs=%lld thread=%lu stage=%s sceneIndex=%lld completed=%llu total=%llu memoryAvailable=%d privateBytes=%llu workingSetBytes=%llu reason=%.512s\n",
            static_cast<unsigned long long>(++state.sequence),
            static_cast<long long>(QDateTime::currentMSecsSinceEpoch()),
#ifdef Q_OS_WIN
            static_cast<unsigned long>(GetCurrentThreadId()),
#else
            0ul,
#endif
            stage,
            static_cast<long long>(index),
            static_cast<unsigned long long>(done),
            static_cast<unsigned long long>(total),
            int(memoryAvailable),
            static_cast<unsigned long long>(privateBytes),
            static_cast<unsigned long long>(workingSet),
            reason ? reason : "");
        std::fflush(state.file);
    }
    catch (...)
    {
        // A diagnostic must never replace the original failure.
    }
}

void terminated() noexcept
{
    const auto exception = std::current_exception();
    if (exception)
    {
        try
        {
            std::rethrow_exception(exception);
        }
        catch (const std::exception& e)
        {
            write("terminate", e.what(), -1, 0, 0);
        }
        catch (...)
        {
            write("terminate", "uncaught non-std C++ exception", -1, 0, 0);
        }
    }
    else
    {
        write("terminate", "no active C++ exception", -1, 0, 0);
    }

    if (const auto previous = trace().previous)
        previous();
    std::abort();
}
}

bool ImportDiagnostics::requested() noexcept
{
    try
    {
        if (qgetenv("MODELVIEWER_IMPORT_DIAGNOSTICS") == "1")
            return true;
        return QSettings().value(QStringLiteral("importDiagnostics"), false).toBool();
    }
    catch (...)
    {
        return false;
    }
}

void ImportDiagnostics::begin() noexcept
{
    try
    {
        if (!requested())
            return;

        auto& state = trace();
        QString path;
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            if (state.file)
                return;
            const auto folder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                + "/logs";
            if (!QDir().mkpath(folder))
                return;
            path = folder + QString("/import-trace-%1-%2.log")
                .arg(QCoreApplication::applicationPid())
                .arg(QDateTime::currentMSecsSinceEpoch());
#ifdef Q_OS_WIN
            state.file = _wfopen(reinterpret_cast<const wchar_t*>(path.utf16()), L"w+b");
#else
            state.file = std::fopen(path.toLocal8Bit().constData(), "w+b");
#endif
            if (!state.file)
                return;
            state.previous = std::set_terminate(terminated);
        }
        qWarning().noquote() << "Import diagnostic trace:" << path;
        checkpoint("trace.begin");
    }
    catch (...)
    {
    }
}

void ImportDiagnostics::checkpoint(const char* stage, qint64 index,
                                   quint64 done, quint64 total) noexcept
{
    write(stage, nullptr, index, done, total);
}

void ImportDiagnostics::failure(const char* stage, const char* reason,
                                qint64 index, quint64 done) noexcept
{
    write(stage, reason, index, done, 0);
}
