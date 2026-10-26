// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#include "ImportDiagnostics.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("ModelViewerImportTraceTests");
    QStandardPaths::setTestModeEnabled(true);
    if (app.arguments().size() > 1)
    {
        const bool terminate = app.arguments().at(1) == "terminate";
        // Test the previous-handler chain without a real abort/WER dialog.
        if (terminate)
            std::set_terminate([] { std::_Exit(86); });
        ImportDiagnostics::begin();
        ImportDiagnostics::checkpoint("test.mesh", 30950, 1, 2);
        ImportDiagnostics::failure("test.failure", "synthetic failure", 30950, 1);
        if (terminate)
        {
            try
            {
                throw std::runtime_error("synthetic uncaught exception");
            }
            catch (...)
            {
                std::terminate();
            }
        }
        return 0;
    }

    try
    {
        for (const QString mode : {QString("trace"), QString("terminate"), QString("disabled")})
        {
            QProcess child;
            auto environment = QProcessEnvironment::systemEnvironment();
            if (mode == "disabled")
                environment.remove("MODELVIEWER_IMPORT_DIAGNOSTICS");
            else
                environment.insert("MODELVIEWER_IMPORT_DIAGNOSTICS", "1");
            child.setProcessEnvironment(environment);
            child.start(app.applicationFilePath(), {mode});
            if (!child.waitForStarted())
                throw std::runtime_error("Trace test child failed to start");

            const auto pid = child.processId();
            if (!child.waitForFinished(30000))
            {
                child.kill();
                child.waitForFinished();
                throw std::runtime_error("Trace test child timed out");
            }
            const int expectedExitCode = mode == "terminate" ? 86 : 0;
            if (child.exitCode() != expectedExitCode)
                throw std::runtime_error("Trace child exit status changed");

            const QDir logs(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/logs");
            const auto files = logs.entryList(
                {QString("import-trace-%1-*.log").arg(pid)}, QDir::Files);
            if (mode == "disabled")
            {
                if (!files.isEmpty())
                    throw std::runtime_error("Disabled trace created a file");
                continue;
            }
            if (files.size() != 1)
                throw std::runtime_error("Missing trace file");

            QFile file(logs.filePath(files.first()));
            if (!file.open(QIODevice::ReadOnly))
                throw std::runtime_error("Cannot read trace file");
            const auto data = file.readAll();
            file.close();
            QFile::remove(file.fileName());
            if (!data.contains("stage=test.mesh sceneIndex=30950 completed=1 total=2")
                || !data.contains("privateBytes=")
                || !data.contains("workingSetBytes=")
                || !data.contains("reason=synthetic failure"))
            {
                throw std::runtime_error("Incomplete trace checkpoint");
            }
            if (mode == "terminate"
                && (!data.contains("stage=terminate")
                    || !data.contains("synthetic uncaught exception")))
            {
                throw std::runtime_error("Terminate diagnostic was not flushed before previous handler exited");
            }
        }
        std::cout << "Import trace and previous terminate-handler chain passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
