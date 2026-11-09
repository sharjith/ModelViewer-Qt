#pragma once

#include <QDebug>
#include <QElapsedTimer>
#include <QString>

// Header-only phase timer for profiling long operations (3D PDF export, MVF load). Each mark() logs the time since the previous mark
// and since the start as one line, "[Profile] <what>: <phase>: <ms> ms (total <ms> ms)", through qDebug (so it reaches the log file).
// Cheap enough to stay in: a few marks per operation, none inside a per-mesh loop.
class PhaseLog
{
public:
	explicit PhaseLog(const QString& what) : _what(what)
	{
		_total.start();
		_lap.start();
	}

	void mark(const QString& phase)
	{
		qDebug().noquote() << QStringLiteral("[Profile] %1: %2: %3 ms (total %4 ms)").arg(_what, phase).arg(_lap.restart()).arg(_total.elapsed());
	}

	// A fact worth keeping next to the timings (a size, a count); does not move the lap.
	void note(const QString& text) const
	{
		qDebug().noquote() << QStringLiteral("[Profile] %1: %2").arg(_what, text);
	}

private:
	QString _what;
	QElapsedTimer _total;
	QElapsedTimer _lap;
};
