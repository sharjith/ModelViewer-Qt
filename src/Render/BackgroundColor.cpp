#include "BackgroundColor.h"
#include "ui_BackgroundColor.h"

#include "LanguageManager.h"
#include "ViewportWidget.h"

#include <QColorDialog>
#include <QMessageBox>

BackgroundColor::BackgroundColor(QWidget* parent) :
	QDialog(parent),
	ui(new Ui::BackgroundColor)
{
	ui->setupUi(this);

	// See ExplodedViewPanel's/ClippingPlanesEditor's identical connection -
	// without this, a live language switch in Settings left this dialog
	// showing whatever language was active at construction until the next
	// app restart.
	connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
		ui->retranslateUi(this);
		});

	ViewportWidget* viewportWidget = dynamic_cast<ViewportWidget*>(parent);
	if (viewportWidget)
	{
		_topColor = viewportWidget->getBgTopColor();
		_bottomColor = viewportWidget->getBgBotColor();
		_gradientStyle = viewportWidget->getBgGradientStyle();
		ui->comboBoxGradientStyle->setCurrentIndex(_gradientStyle);
		QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
		int styleIndex = settings.value("Background/StyleIndex", 0).toInt();
		ui->checkBoxGrad->setChecked(styleIndex != 1); // 0=Gradient → checked, 1=Solid → unchecked
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
	ViewportWidget* viewportWidget = dynamic_cast<ViewportWidget*>(parent());
	if (viewportWidget)
	{
		viewportWidget->setBgTopColor(_topColor);
		if (hasGradient())
			viewportWidget->setBgBotColor(_bottomColor);
		else
			viewportWidget->setBgBotColor(_topColor);
		viewportWidget->setBgGradientStyle(_gradientStyle);
		saveSettings();
		viewportWidget->loadBgColorSettings(); // sync bgStyleIndex on render controller
	}
	else
	{
		saveSettings();
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

void BackgroundColor::on_comboBoxGradientStyle_currentIndexChanged(int index)
{
	_gradientStyle = index;
	setPreviewColor();
}

void BackgroundColor::saveSettings()
{
	QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());

	// Store top color
	settings.setValue("Background/TopColor", _topColor);

	// Store bottom color
	settings.setValue("Background/BottomColor", _bottomColor);

	// Store gradient style
	settings.setValue("Background/GradientStyle", _gradientStyle);

	// Store style mode (0=Gradient, 1=Solid) to stay in sync with SettingsDialog
	settings.setValue("Background/StyleIndex", hasGradient() ? 0 : 1);
}

void BackgroundColor::on_cancelButton_clicked()
{
	QDialog::reject();
}

void BackgroundColor::setPreviewColor()
{
	QString gradientDirection;

	switch (_gradientStyle)
	{
	case 0: // Vertical gradient (top to bottom)
		gradientDirection = "x1:0, y1:0, x2:0, y2:1";
		break;
	case 1: // Horizontal gradient (left to right)
		gradientDirection = "x1:0, y1:0, x2:1, y2:0";
		break;
	case 2: // Diagonal gradient (top-left to bottom-right)
		gradientDirection = "x1:0, y1:0, x2:1, y2:1";
		break;
	case 3: // Diagonal gradient (top-right to bottom-left)
		gradientDirection = "x1:1, y1:0, x2:0, y2:1";
		break;
	default: // Default to vertical gradient
		gradientDirection = "x1:0, y1:0, x2:0, y2:1";
		break;
	}

	QString col = QString::fromUtf8("background-color: qlineargradient(spread:pad, %7, "
		"stop:0 rgba(%1, %2, %3, 255), "
		"stop:1 rgba(%4, %5, %6, 255));")
		.arg(_topColor.red()).arg(_topColor.green()).arg(_topColor.blue())
		.arg(_bottomColor.red()).arg(_bottomColor.green()).arg(_bottomColor.blue())
		.arg(gradientDirection);

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
	_gradientStyle = 0; // Default to vertical gradient
	ui->comboBoxGradientStyle->setCurrentIndex(_gradientStyle);
    setPreviewColor();
}
