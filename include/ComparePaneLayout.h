#pragma once

// Pane geometry for the simulation Compare mode - docs/simulation_compare_mode_design.md. Pure geometry (QtCore
// only), unit-tested in result_tests.
//
// Each pane shows one result. A pane is NOT rendered with a narrower camera: it is the normal full-window view
// (same aspect ratio, same projection, so every camera interaction and picking calculation keeps working unchanged)
// drawn into a full-size viewport that is shifted so the model appears centred in the pane, and clipped to the pane's
// rectangle with a scissor. A point inside a pane therefore corresponds to a point of the full-window view: add
// `toWindow`.

#include <QPoint>
#include <QRect>

#include <vector>

enum class CompareArrangement
{
	SideBySide, // panes in a row
	Stacked,    // panes in a column
	Grid        // 2 x 2 (for three or four panes)
};

struct ComparePane
{
	QRect rect;       // the visible pane, widget coordinates (origin top-left, y down) - for mouse events and overlays
	QRect glScissor;  // the same rectangle in GL window coordinates (origin bottom-left) - for glScissor
	QRect glViewport; // full-window-size viewport centred on the pane (x/y can be negative) - for glViewport
	QPoint toWindow;  // pane point + toWindow = the equivalent point of the full-window view
};

// 1 to 4 panes (`count` is clamped). `gutter` pixels separate the panes; a window too small for the gutters gets
// zero-width gutters instead of negative panes. Returns `count` panes in reading order.
std::vector<ComparePane> computeComparePanes(int width, int height, int count, CompareArrangement arrangement, int gutter = 2);

// Index of the pane containing `point` (widget coordinates), or -1 for a gutter or a point outside every pane.
int comparePaneAt(const std::vector<ComparePane>& panes, const QPoint& point);
