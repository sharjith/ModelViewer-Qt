#include "ComparePaneLayout.h"

#include <algorithm>

namespace
{
	ComparePane makePane(const QRect& rect, int width, int height)
	{
		ComparePane pane;
		pane.rect = rect;
		// GL's origin is the bottom-left corner.
		pane.glScissor = QRect(rect.x(), height - (rect.y() + rect.height()), rect.width(), rect.height());
		const int centreX = rect.x() + rect.width() / 2;
		const int centreY = rect.y() + rect.height() / 2;
		pane.toWindow = QPoint(width / 2 - centreX, height / 2 - centreY);
		// The full-window view centred on the pane: its centre (viewport x + width/2, viewport y + height/2, GL
		// coordinates) sits on the pane's centre.
		pane.glViewport = QRect(centreX - width / 2, (height - centreY) - height / 2, width, height);
		return pane;
	}

	// Splits `total` pixels into `n` parts separated by `gutter`; the last part takes the rounding remainder.
	void split(int total, int n, int gutter, std::vector<int>& starts, std::vector<int>& sizes)
	{
		const int usable = std::max(0, total - (n - 1) * gutter);
		const int each = usable / n;
		for (int i = 0; i < n; ++i)
		{
			starts.push_back(std::min(total, i * (each + gutter))); // a window too small for the gutters gets empty panes at its edge
			sizes.push_back(i == n - 1 ? usable - each * (n - 1) : each);
		}
	}
}

std::vector<ComparePane> computeComparePanes(int width, int height, int count, CompareArrangement arrangement, int gutter)
{
	width = std::max(1, width);
	height = std::max(1, height);
	count = std::clamp(count, 1, 4);
	gutter = std::max(0, gutter);

	std::vector<ComparePane> panes;
	if (count == 1)
	{
		panes.push_back(makePane(QRect(0, 0, width, height), width, height));
		return panes;
	}

	if (arrangement == CompareArrangement::Grid && count >= 3)
	{
		std::vector<int> xs, ws, ys, hs;
		split(width, 2, gutter, xs, ws);
		split(height, 2, gutter, ys, hs);
		for (int i = 0; i < count; ++i)
		{
			const int col = i % 2, row = i / 2;
			panes.push_back(makePane(QRect(xs[static_cast<std::size_t>(col)], ys[static_cast<std::size_t>(row)],
			                               ws[static_cast<std::size_t>(col)], hs[static_cast<std::size_t>(row)]), width, height));
		}
		return panes;
	}

	std::vector<int> starts, sizes;
	if (arrangement == CompareArrangement::Stacked)
	{
		split(height, count, gutter, starts, sizes);
		for (int i = 0; i < count; ++i)
			panes.push_back(makePane(QRect(0, starts[static_cast<std::size_t>(i)], width, sizes[static_cast<std::size_t>(i)]), width, height));
	}
	else // SideBySide, and Grid with fewer than three panes
	{
		split(width, count, gutter, starts, sizes);
		for (int i = 0; i < count; ++i)
			panes.push_back(makePane(QRect(starts[static_cast<std::size_t>(i)], 0, sizes[static_cast<std::size_t>(i)], height), width, height));
	}
	return panes;
}

int comparePaneAt(const std::vector<ComparePane>& panes, const QPoint& point)
{
	for (std::size_t i = 0; i < panes.size(); ++i)
		if (panes[i].rect.contains(point))
			return static_cast<int>(i);
	return -1;
}
