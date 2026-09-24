#pragma once

#include <QAbstractButton>
#include <QLabel>
#include <QWidget>

// A button's, check box's or label's minimum width normally follows its text, so a long translation forces the
// dock hosting it wider - pushing into the viewport. An explicit minimum width of 1 overrides that: layouts
// may then squeeze the widget below its text width (the text clips) instead of growing the dock. Only widgets
// that show text and have no minimum width of their own are touched, so icon-only buttons keep their size.
inline void allowTextToShrink(QWidget* root)
{
    if (!root)
        return;
    for (QAbstractButton* button : root->findChildren<QAbstractButton*>())
        if (!button->text().isEmpty() && button->minimumWidth() == 0)
            button->setMinimumWidth(1);
    for (QLabel* label : root->findChildren<QLabel*>())
        if (!label->text().isEmpty() && !label->wordWrap() && label->minimumWidth() == 0)
            label->setMinimumWidth(1);
}
