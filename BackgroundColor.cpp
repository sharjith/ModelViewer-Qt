#include "BackgroundColor.h"
#include "ui_BackgroundColor.h"

#include "GLWidget.h"

#include <QColorDialog>
#include <QMessageBox>

BackgroundColor::BackgroundColor(QWidget* parent) :
	QDialog(parent),
	ui(new Ui::BackgroundColor)
{
	ui->setupUi(this);

	GLWidget* glWidget = dynamic_cast<GLWidget*>(parent);
	if (glWidget)
	{
		_topColor = glWidget->getBgTopColor();
		_bottomColor = glWidget->getBgBotColor();
		setPreviewColor();
	}
}

BackgroundColor::~BackgroundColor()
{
	delete ui;
}

bool BackgroundColor::hasGradient() const
{
	return ui->checkBoxGrad->isChecked();
}

void BackgroundColor::applyBgColors()
{
	GLWidget* glWidget = dynamic_cast<GLWidget*>(parent());
	if (glWidget)
	{
		glWidget->setBgTopColor(_topColor);
		if (hasGradient())
			glWidget->setBgBotColor(_bottomColor);
		else
			glWidget->setBgBotColor(_topColor);
	}
}

void BackgroundColor::on_okButton_clicked()
{
	applyBgColors();
	QDialog::accept();
}

void BackgroundColor::on_applyButton_clicked()
{
	applyBgColors();
}

void BackgroundColor::on_cancelButton_clicked()
{
	QDialog::reject();
}

void BackgroundColor::setPreviewColor()
{
	QString col = QString::fromUtf8("background-color: qlineargradient(spread:pad, x1:0, y1:0, x2:0, y2:1, "
		"stop:0 rgba(%1, %2, %3, 255), "
		"stop:1 rgba(%4, %5, %6, 255));").arg(_topColor.red()).arg(_topColor.green()).arg(_topColor.blue())
		.arg(_bottomColor.red()).arg(_bottomColor.green()).arg(_bottomColor.blue());
	ui->labelColorPreview->setStyleSheet(col);
	ui->labelColorPreview->update();
}

void BackgroundColor::on_pushButtonTop_clicked()
{
	QColor color = QColorDialog::getColor(_topColor, this);
	if (color.isValid())
	{
		_topColor = color;
		if (!hasGradient())
			_bottomColor = _topColor;
		setPreviewColor();
	}
}

void BackgroundColor::on_pushButtonBottom_clicked()
{
	QColor color = QColorDialog::getColor(_bottomColor, this);
	if (color.isValid())
	{
		_bottomColor = color;
		setPreviewColor();
	}
}

void BackgroundColor::on_pushButtonDefaultColor_clicked()
{
    _topColor = QColor::fromRgbF(0.45f, 0.45f, 0.45f, 1.0f);
    _bottomColor = QColor::fromRgbF(0.9f, 0.9f, 0.9f, 1.0f);
    setPreviewColor();
}
