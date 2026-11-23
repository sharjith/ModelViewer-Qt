#pragma once

#include <QWidget>

class QCheckBox;
class QEvent;
class QShowEvent;
class QVBoxLayout;

// The Start Page: the application's front door, shown as a (closable) document tab instead of a dialog. It gathers what used to be split
// between the Quick Help "Home" tab and the What's New popup (the notes are now the highlights card, with the full release notes on GitHub): open a model, the recently opened files, the bundled samples, a pointer to
// what the current release added, and the first steps. It owns no application logic - every action is a signal that MainWindow connects
// to the existing open / tutorial / help code.
//
// Colours come from the application palette (not fixed values), so the page follows the light and dark themes.
class StartPageWidget : public QWidget
{
	Q_OBJECT
public:
	explicit StartPageWidget(QWidget* parent = nullptr);

	// Rebuilds the "Recent Files" list from the saved settings (also done every time the page is shown).
	void refreshRecentFiles();

signals:
	void openModelRequested();
	void openFileRequested(const QString& path);
	void openSampleRequested(const QString& relativeModelPath); // relative to the installed sample-models folder
	void tutorialRequested();
	void lessonRequested(int lesson); // a tutorial lesson linked from the What's New card
	void shortcutsRequested();
	void mouseControlsRequested();

protected:
	void showEvent(QShowEvent* event) override;
	void changeEvent(QEvent* event) override;

private:
	void buildUi();
	// The "Video Tutorials" row, from data/start-page/videos.json; null when the manifest is missing or empty.
	QWidget* buildVideoPanel(QWidget* parent);
	void applyTheme();

	QVBoxLayout* _recentLayout = nullptr;
	QCheckBox* _showOnStartup = nullptr;
	bool _whatsNewDue = false; // this version's notes have not been seen yet: the card is highlighted
	bool _applyingTheme = false;
};
