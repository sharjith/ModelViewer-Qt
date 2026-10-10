#pragma once

#include <QDialog>

class QCheckBox;
class QPushButton;
class QSettings;
class QTextBrowser;

// "What's new in this version": a short, illustrated list of what the release added and where to find it, with buttons that lead on to the tutorial and
// Quick Help. It opens by itself once after an update (and on a fresh install) unless the user turned that off, and any time from Help > What's New.
// The notes are translated text (tr), so they follow the application language.
class WhatsNewDialog : public QDialog
{
	Q_OBJECT
public:
	explicit WhatsNewDialog(QWidget* parent = nullptr);

	// True when this version's notes have not been shown yet and the user has not switched the automatic dialog off.
	static bool dueOnStartup(const QSettings& settings);
	// Remembers that this version's notes were shown (called when the startup rule opens the dialog, so a crash cannot make it appear every launch).
	static void markShown();

signals:
	void openTutorialRequested(int lesson); // 0 = the tutorial home, otherwise the lesson number
	void openQuickHelpRequested();

private:
	QString buildHtml() const;

	QTextBrowser* _browser = nullptr;
	QCheckBox* _showOnUpdate = nullptr;
	QPushButton* _tutorialButton = nullptr;
	QPushButton* _helpButton = nullptr;
	QPushButton* _closeButton = nullptr;
};
