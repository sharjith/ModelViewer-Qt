#pragma once

#include <QString>

class QAbstractItemView;
class QLabel;
class QVBoxLayout;
class QWidget;

// Small helpers for the tool dialogs' default ("nothing selected yet") layout.
//
// A word-wrapped QLabel has a growable height by default, so whenever the space a dialog is given exceeds what its
// content needs - typically because the tables/lists that normally fill it are hidden or empty - the layout hands the
// slack to those labels and their text ends up floating in the middle of empty space. These helpers keep such labels at
// their natural height, and give the empty state a deliberate look instead.
namespace DialogLayout
{
	// The label keeps its natural height instead of being stretched by leftover space.
	void keepNaturalHeight(QLabel* label);

	// The label is an empty-state hint: it takes the space the hidden content would fill and sits centred in it.
	// (Give it a stretch factor in its box layout, e.g. layout->addWidget(label, 1).)
	void makeCentredHint(QLabel* label);

	// Puts a dialog's action row at the bottom: the status label, then the action button, in that order, after everything
	// else in `layout`. With `packAtTop` a spacer goes in front of them, so the controls above stay at the top and the
	// spare height sits between them and the button (for a dialog that has no widget of its own that expands).
	// The status label keeps its natural height.
	void pinActionToBottom(QVBoxLayout* layout, QLabel* statusLabel, QWidget* actionButton, bool packAtTop);

	// While the view has no rows it shows `text` centred and muted on its background (a placeholder, which a plain
	// list/table widget does not have); the text disappears as soon as a row exists.
	void attachEmptyHint(QAbstractItemView* view, const QString& text);
}
