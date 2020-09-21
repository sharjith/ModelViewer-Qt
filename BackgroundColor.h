#ifndef BACKGROUNDCOLOR_H
#define BACKGROUNDCOLOR_H

#include <QDialog>

namespace Ui {
	class BackgroundColor;
}

class BackgroundColor : public QDialog
{
	Q_OBJECT

public:
	explicit BackgroundColor(QWidget* parent = 0);
	~BackgroundColor();

	bool hasGradient() const;

	QColor getTopColor() const { return _topColor; }
	QColor getBottomColor() const { return _bottomColor; }

	void applyBgColors();

	void setPreviewColor();

private slots:
	void on_okButton_clicked();

	void on_cancelButton_clicked();

	void on_pushButtonTop_clicked();

	void on_pushButtonBottom_clicked();

	void on_pushButtonDefaultColor_clicked();

	void on_applyButton_clicked();

private:
	Ui::BackgroundColor* ui;

	QColor _topColor;
	QColor _bottomColor;
};

#endif // BACKGROUNDCOLOR_H
