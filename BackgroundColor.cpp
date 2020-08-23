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
		topColor = glWidget->getBgTopColor();
		QPalette pal = ui->labelTopColor->palette();
		pal.setColor(QPalette::Window, topColor);
		ui->labelTopColor->setAutoFillBackground(true);
		ui->labelTopColor->setPalette(pal);
		ui->labelTopColor->update();

		pal = ui->labelBotColor->palette();
		bottomColor = glWidget->getBgBotColor();
		pal.setColor(QPalette::Window, bottomColor);
		ui->labelBotColor->setAutoFillBackground(true);
		ui->labelBotColor->setPalette(pal);
		ui->labelBotColor->update();
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
		QPalette pal = ui->labelTopColor->palette();
		QColor topColor = pal.color(QPalette::Window);
		glWidget->setBgTopColor(topColor);
		if (hasGradient())
		{
			pal = ui->labelBotColor->palette();
			QColor botColor = pal.color(QPalette::Window);
			glWidget->setBgBotColor(botColor);
		}
		else
			glWidget->setBgBotColor(topColor);
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

void BackgroundColor::on_pushButtonTop_clicked()
{
	QColor initColor = QColor::fromRgbF(0.3f, 0.3f, 0.3f, 1.0f);
	topColor = QColorDialog::getColor(initColor, this);
	if (topColor.isValid())
	{
		GLWidget* glWidget = dynamic_cast<GLWidget*>(parent());
		if (glWidget)
		{
			initColor = glWidget->getBgTopColor();

			QPalette pal = ui->labelTopColor->palette();
			pal.setColor(QPalette::Window, topColor);
			ui->labelTopColor->setAutoFillBackground(true);
			ui->labelTopColor->setPalette(pal);
			ui->labelTopColor->update();
		}
	}
}

void BackgroundColor::on_pushButtonBottom_clicked()
{
	QColor initColor = QColor::fromRgbF(0.925f, 0.913f, 0.847f, 1.0f);
	bottomColor = QColorDialog::getColor(initColor, this);
	if (bottomColor.isValid())
	{
		GLWidget* glWidget = dynamic_cast<GLWidget*>(parent());
		if (glWidget)
		{
			initColor = glWidget->getBgBotColor();

			QPalette pal = ui->labelBotColor->palette();
			pal.setColor(QPalette::Window, bottomColor);
			ui->labelBotColor->setAutoFillBackground(true);
			ui->labelBotColor->setPalette(pal);
			ui->labelBotColor->update();
		}
	}
}

void BackgroundColor::on_pushButtonDefaultColor_clicked()
{
	QColor col = QColor::fromRgbF(0.3f, 0.3f, 0.3f, 1.0f);
	QPalette pal = ui->labelTopColor->palette();
	pal.setColor(QPalette::Window, col);
	ui->labelTopColor->setAutoFillBackground(true);
	ui->labelTopColor->setPalette(pal);
	ui->labelTopColor->update();

	col = QColor::fromRgbF(0.925f, 0.913f, 0.847f, 1.0f);
	pal = ui->labelBotColor->palette();
	pal.setColor(QPalette::Window, col);
	ui->labelBotColor->setAutoFillBackground(true);
	ui->labelBotColor->setPalette(pal);
	ui->labelBotColor->update();
}