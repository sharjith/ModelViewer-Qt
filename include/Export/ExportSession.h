#pragma once

#include <QElapsedTimer>
#include <QString>
#include <QStringList>

// Everything that belongs to one export and to nothing else: the timing lines and the warnings collected while writing.
// An own writer creates one on the stack per export, so nothing is shared between exports (no process-wide state).
class ExportSession
{
public:
    explicit ExportSession(const QString& what);

    // Logs one "[ExportProfile]" line: wall-clock time of the phase that just ended, and the total so far.
    void phase(const char* name);

    // Records something the file cannot represent (also logged).
    void warn(const QString& text);

    const QStringList& warnings() const { return _warnings; }
    qint64 elapsedMs() const { return _timer.elapsed(); }

private:
    QString _what;
    QElapsedTimer _timer;
    qint64 _last = 0;
    QStringList _warnings;
};
