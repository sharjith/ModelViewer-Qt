#pragma once

#include <QUrl>

class QSettings;

// The "what's new" bookkeeping for the Start Page. The notes themselves are the highlights card on the Start Page (translated text) and
// the full release notes on GitHub; there is no separate dialog.
namespace WhatsNew
{
	// True when this version's notes have not been shown yet (the Start Page then opens after an update whatever the startup option says).
	bool dueOnStartup(const QSettings& settings);
	// Remembers that this version's notes were shown (called when the startup rule opens the Start Page, so a crash cannot make it appear every launch).
	void markShown();
	// The GitHub release page of the running version.
	QUrl releaseNotesUrl();
}
