#pragma once

#include <QTreeWidget>

class CapturedStepsTreeWidget : public QTreeWidget
{
    Q_OBJECT

public:
    explicit CapturedStepsTreeWidget(QWidget* parent = nullptr);
};
