#pragma once

#include <QToolButton>
#include <QPainter>
#include <QPolygon>
#include <QBrush>
#include <QColor>
#include <QPaintEvent>
#include <QWidget>

// FlyOutViewButton.h
// Custom button with an upward arrow at the top-right corner
// of the button, used for fly-out views in a user interface.
// The button inherits from QToolButton and overrides the paintEvent
// to draw the custom arrow.

class FlyOutViewButton : public QToolButton
{
public:
	static QString menuStyleSheet()
	{
		return QStringLiteral(
        "QMenu {"
        "    background-color: rgba(255, 255, 255, 100);"
        "    border: 1px solid gray;"
        "    border-radius: 4px;"
        "    padding: 2px;"
        "    icon-size: 36px;"
        "}"
        "QMenu::item {"
        "    background: transparent;"
        "    background-color: #f0f0f0;"
        "    border: 1px solid #c0c0c0;"
        "    border-radius: 4px;"
        "    padding: 5px 8px;"
        "    margin: 3px;"
        "    min-width: 120px;"
        "    min-height: 30px;"
        "    font-weight: normal;"
        "    color: black;"
        "}"
        "QMenu::item:selected {"
        "    background-color: #e0e0ff;"
        "    border: 1px solid #a0a0ff;"
        "    color: black;"
        "}"
        "QMenu::item:pressed {"
        "    background-color: #d0d0ff;"
        "    border: 1px solid #8080ff;"
        "    color: black;"
        "}"
        "QMenu::icon {"
        "    padding-left: 10px;"
        "    padding-right: 8px;"
        "}"
        "QMenu::separator {"
        "    height: 1px;"
        "    background-color: #c0c0c0;"
        "    margin: 4px 8px;"
        "}"

		);
	}

	FlyOutViewButton(QWidget* parent = nullptr) : QToolButton(parent)
	{
		setStyleSheet(
			"QToolButton {"
			"    border: none;"
			"    background: transparent;"
			"    padding: 5px;"
			"    border-radius: 4px;"
			"}"
			// Set from code (dynamic property) while the view matches what the button represents,
			// e.g. the axonometric type/corner buttons while the view is axonometric. Listed before
			// :hover/:pressed so their feedback still shows on top of it.
			"QToolButton[viewActive=\"true\"] {"
			"    background-color: rgba(0, 150, 100, 100);"
			"    border: 1px solid #008000;"
			"}"
			"QToolButton:hover {"
			"    background-color: rgba(0, 120, 215, 50);"
			"    border: 1px solid #0078D7;"
			"}"
			"QToolButton:pressed {"
			"    background-color: rgba(0, 120, 215, 100);"
			"    border: 1px solid #005A9E;"
			"}"
			"QToolButton:checked {"
			"    background-color: rgba(0, 150, 100, 100);"
			"    border: 1px solid #008000;"
			"    color: white;"
			"}"
			"QToolButton::menu-indicator {"
			"    image: none;"
			"    width: 0px;"
			"    height: 0px;"
			"}"
		);
	}

protected:
	void paintEvent(QPaintEvent* event) override
	{
		QToolButton::paintEvent(event);

		// Draw custom up arrow at the top of the button
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);

		int arrowSize = 6;
		QPoint arrowTip(width() - 12, 5); // Top-right area

		// Create upward triangle
		QPolygon triangle;
		triangle << arrowTip  // tip pointing up
			<< QPoint(arrowTip.x() - arrowSize, arrowTip.y() + arrowSize)  // bottom left
			<< QPoint(arrowTip.x() + arrowSize, arrowTip.y() + arrowSize); // bottom right

		painter.setBrush(QBrush(QColor(51, 51, 51)));
		painter.setPen(Qt::NoPen);
		painter.drawPolygon(triangle);
	}
};
