#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QLabel;
class QListWidget;

// ---------------------------------------------------------------------------
// NotesListBox
//
// A compact, height-capped list for the messages a tool dialog wants to show ABOUT its result - repair notes,
// rejection reasons, footnotes - instead of a word-wrapped label that grows with every mesh (and pushes the dialog
// past the screen when many meshes are involved).
//   * A one-line title "Notes (N)" over a bordered list of entries with an info or warning icon; entries wrap, and
//     the list never grows beyond a fixed maximum height - beyond that it scrolls.
//   * Entries that carry a subject (a mesh name) and share the same message are grouped: "12 meshes: not closed"
//     rather than twelve lines; the tooltip names them.
//   * Hidden while empty, so a clean result costs no space. Right-click: Copy All.
// ---------------------------------------------------------------------------
class NotesListBox : public QWidget
{
	Q_OBJECT
public:
	enum class Severity { Info, Warning };

	struct Note
	{
		QString subject;   // e.g. the mesh name; empty for a note about the whole result
		QString message;
		Severity severity = Severity::Info;
	};

	explicit NotesListBox(QWidget* parent = nullptr);

	// Replaces the notes (grouping equal messages); an empty list hides the box.
	void setNotes(const QVector<Note>& notes);
	void clearNotes() { setNotes(QVector<Note>()); }
	// Number of rows shown (after grouping).
	int rowCount() const;

	// The most the list may grow to, in pixels (default about five text lines); more rows scroll.
	void setMaximumListHeight(int pixels);

protected:
	void resizeEvent(QResizeEvent* event) override;

private:
	void refit();
	void copyAll();

	QLabel* _title = nullptr;
	QListWidget* _list = nullptr;
	int _maxListHeight = 96;
	QStringList _plainTexts; // one full text per row, for Copy All
};
