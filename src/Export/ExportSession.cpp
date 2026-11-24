#include "ExportSession.h"

#include <QDebug>

ExportSession::ExportSession(const QString& what)
    : _what(what)
{
    _timer.start();
}

void ExportSession::phase(const char* name)
{
    const qint64 now = _timer.elapsed();
    qDebug().noquote() << QStringLiteral("[ExportProfile] %1 / %2: %3 ms (total %4 ms)")
        .arg(_what, QString::fromLatin1(name)).arg(now - _last).arg(now);
    _last = now;
}

void ExportSession::warn(const QString& text)
{
    _warnings.append(text);
    qWarning().noquote() << QStringLiteral("[Export] %1: %2").arg(_what, text);
}
