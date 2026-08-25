#include "ClippingPlanesEditor.h"
#include "ui_ClippingPlanesEditor.h"
#include "LanguageManager.h"
#include "ViewportWidget.h"
#include "PathUtils.h"
#include <QCheckBox>
#include <QKeyEvent>
#include <QColorDialog>
#include <QFileDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QPainter>
#include <QProxyStyle>
#include <QRadioButton>
#include <QStyleOptionButton>
#include <QUrl>

// helper: simple extension check (same filters as file dialog)
static bool isImageFileExtension(const QString& path)
{
	const QStringList exts = { ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".hdr", ".exr" };
	const QString lower = path.toLower();
	for (const QString& e : exts)
		if (lower.endsWith(e)) return true;
	return false;
}

// Filter that only uses the button and glView pointers it was given.
class TextureButtonDropFilter : public QObject
{
public:
	// pass the specific UI button and the GL view; parent the filter to 'parent'
	explicit TextureButtonDropFilter(QPushButton* button, ViewportWidget* viewportWidget, QObject* parent = nullptr)
		: QObject(parent), _button(button), _viewportWidget(viewportWidget)
	{
	}

protected:
	bool eventFilter(QObject* obj, QEvent* ev) override
	{
		Q_UNUSED(obj);
		if (ev->type() == QEvent::DragEnter)
		{
			QDragEnterEvent* den = static_cast<QDragEnterEvent*>(ev);
			const QMimeData* md = den->mimeData();
			if (md && md->hasUrls())
			{
				const QList<QUrl> urls = md->urls();
				if (!urls.isEmpty())
				{
					const QString local = urls.first().toLocalFile();
					if (!local.isEmpty() && isImageFileExtension(local))
					{
						den->acceptProposedAction();
						return true;
					}
				}
			}
			return QObject::eventFilter(obj, ev);
		}
		else if (ev->type() == QEvent::Drop)
		{
			QDropEvent* de = static_cast<QDropEvent*>(ev);
			const QMimeData* md = de->mimeData();
			if (md && md->hasUrls())
			{
				const QList<QUrl> urls = md->urls();
				if (!urls.isEmpty())
				{
					const QString local = urls.first().toLocalFile();
					if (!local.isEmpty() && isImageFileExtension(local))
					{
						// Apply same behaviour as on_pushButtonTexture_clicked
						if (_button)
						{
							int thumb = 140;
							_button->setFixedSize(thumb, thumb);

							QPixmap pix(local);
							QIcon icon(pix);
							_button->setIcon(icon);
							_button->setIconSize(_button->size());
							_button->setText(QString());
							_button->setToolTip(QFileInfo(local).fileName());
						}

						if (_viewportWidget)
						{
							_viewportWidget->setHatchTexture(local);
							_viewportWidget->updateClippingPlane();
							_viewportWidget->update();
						}

						de->acceptProposedAction();
						return true;
					}
				}
			}
			return QObject::eventFilter(obj, ev);
		}

		return QObject::eventFilter(obj, ev);
	}

private:
	QPushButton* _button = nullptr;
	ViewportWidget* _viewportWidget = nullptr;
};

class OverlayEditorCheckBoxStyle : public QProxyStyle
{
public:
	using QProxyStyle::QProxyStyle;

	void drawPrimitive(PrimitiveElement pe,
		const QStyleOption* opt,
		QPainter* painter,
		const QWidget* widget = nullptr) const override
	{
		if (pe != PE_IndicatorCheckBox || !widget)
		{
			QProxyStyle::drawPrimitive(pe, opt, painter, widget);
			return;
		}

		QStyleOptionButton buttonOpt;
		if (const auto* button = qstyleoption_cast<const QStyleOptionButton*>(opt))
			buttonOpt = *button;
		else if (opt)
			buttonOpt.rect = opt->rect;
		else
			buttonOpt.initFrom(widget);

		const bool lightText = widget->property("overlayIndicatorLightText").toBool();
		const QColor boxFill = lightText ? QColor(24, 24, 24, 220) : QColor(255, 255, 255, 225);
		const QColor boxBorder = lightText ? QColor(255, 255, 255, 155) : QColor(0, 0, 0, 110);
		const QColor markColor = lightText ? QColor(255, 255, 255) : QColor(0, 0, 0);

		const QRect rect = buttonOpt.rect.adjusted(1, 1, -1, -1);
		painter->save();
		painter->setRenderHint(QPainter::Antialiasing, true);
		painter->setPen(QPen(boxBorder, (buttonOpt.state & State_MouseOver) ? 1.2 : 1.0));
		painter->setBrush(boxFill);
		painter->drawRoundedRect(rect, 2.0, 2.0);

		if (buttonOpt.state & State_On)
		{
			const QPoint p1(rect.left() + 3, rect.center().y());
			const QPoint p2(rect.center().x() - 1, rect.bottom() - 3);
			const QPoint p3(rect.right() - 2, rect.top() + 3);
			painter->setPen(QPen(markColor, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter->drawLine(p1, p2);
			painter->drawLine(p2, p3);
		}
		else if (buttonOpt.state & State_NoChange)
		{
			painter->setPen(QPen(markColor, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter->drawLine(rect.left() + 3, rect.center().y(),
				rect.right() - 3, rect.center().y());
		}

		painter->restore();
	}
};

void installOverlayEditorCheckBoxStyle(QCheckBox* box)
{
	if (!box)
		return;

	box->setProperty("overlayIndicatorLightText", false);
	box->setStyle(new OverlayEditorCheckBoxStyle(box->style()));
}


ClippingPlanesEditor::ClippingPlanesEditor(ViewportWidget* parent) :
	QWidget(parent),
	_viewportWidget(parent)
{
	setupUi(this);
	for (QCheckBox* box : findChildren<QCheckBox*>())
		installOverlayEditorCheckBoxStyle(box);

	connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
		retranslateUi(this);
		});

	// enable drag/drop on the single texture button (no header changes)
	pushButtonTexture->setAcceptDrops(true);
	// parent the filter to 'this' so it will be deleted with the editor
	pushButtonTexture->installEventFilter(new TextureButtonDropFilter(pushButtonTexture, _viewportWidget, this));
}

ClippingPlanesEditor::~ClippingPlanesEditor()
{
}

void ClippingPlanesEditor::applyContrastTheme(const QColor& textColor)
{
	const QString editorStyle = QString("color: rgb(%1, %2, %3);")
		.arg(textColor.red())
		.arg(textColor.green())
		.arg(textColor.blue());
	setStyleSheet(editorStyle);

	const QString blackTextStyle = QStringLiteral("color: rgb(0, 0, 0);");
	const bool lightText = textColor.lightnessF() >= 0.5;
	const QColor indicatorFill = lightText ? QColor(24, 24, 24, 160) : QColor(255, 255, 255, 180);
	const QString radioIndicatorStyle = QString(
		"QRadioButton { color: rgb(%1, %2, %3); }"
		"QRadioButton::indicator {"
		" width: 13px;"
		" height: 13px;"
		" border-radius: 6.5px;"
		" border: 1px solid rgba(%1, %2, %3, 220);"
		" background-color: rgba(%4, %5, %6, %7);"
		"}"
		"QRadioButton::indicator:checked {"
		" border: 1px solid rgba(%1, %2, %3, 220);"
		" background-color: qradialgradient("
		"   cx:0.5, cy:0.5, radius:0.5, fx:0.5, fy:0.5,"
		"   stop:0 rgb(%1, %2, %3),"
		"   stop:0.6 rgb(%1, %2, %3),"
		"   stop:0.65 transparent,"
		"   stop:1 transparent);"
		"}"
		"QRadioButton::indicator:unchecked:hover,"
		"QRadioButton::indicator:checked:hover {"
		" border: 1px solid rgb(%1, %2, %3);"
		"}")
		.arg(textColor.red())
		.arg(textColor.green())
		.arg(textColor.blue())
		.arg(indicatorFill.red())
		.arg(indicatorFill.green())
		.arg(indicatorFill.blue())
		.arg(indicatorFill.alpha());
	pushButtonResetCoeffs->setStyleSheet(blackTextStyle);
	pushButtonDefaultValues->setStyleSheet(blackTextStyle);
	pushButtonResetAll->setStyleSheet(blackTextStyle);
	pushButtonTexture->setStyleSheet(QStringLiteral("background-color: rgba(255, 255, 255, 5%); color: rgb(0, 0, 0);"));
	comboBoxHatchMode->setStyleSheet(blackTextStyle);
	radioButtonProcedural->setStyleSheet(radioIndicatorStyle);
	radioButtonTextured->setStyleSheet(radioIndicatorStyle);

	for (QCheckBox* box : findChildren<QCheckBox*>())
	{
		box->setProperty("overlayIndicatorLightText", lightText);
		box->style()->unpolish(box);
		box->style()->polish(box);
		box->update();
	}
}

void ClippingPlanesEditor::applyBackgroundTheme(const QColor& topColor, const QColor& bottomColor)
{
	const QColor averageBackgroundColor(
		(topColor.red() + bottomColor.red()) / 2,
		(topColor.green() + bottomColor.green()) / 2,
		(topColor.blue() + bottomColor.blue()) / 2,
		(topColor.alpha() + bottomColor.alpha()) / 2);
	const QColor contrastColor = (averageBackgroundColor.lightnessF() < 0.5)
		? QColor(255, 255, 255)
		: QColor(0, 0, 0);

	applyContrastTheme(contrastColor);
}

void ClippingPlanesEditor::setCoefficientLimits(double xMin, double xMax, double yMin, double yMax, double zMin, double zMax)
{
	doubleSpinBoxXYCoeff->setRange(zMin, zMax);
	doubleSpinBoxYZCoeff->setRange(xMin, xMax);
	doubleSpinBoxZXCoeff->setRange(yMin, yMax);

	// Set the step as 1/50th of the range
	doubleSpinBoxXYCoeff->setSingleStep((zMax - zMin) / 50.0);
	doubleSpinBoxYZCoeff->setSingleStep((xMax - xMin) / 50.0);
	doubleSpinBoxZXCoeff->setSingleStep((yMax - yMin) / 50.0);
}

void ClippingPlanesEditor::keyPressEvent(QKeyEvent* e)
{
	if (e->key() != Qt::Key_Escape)
		QWidget::keyPressEvent(e);
	else {/* minimize */ }
}

void ClippingPlanesEditor::on_checkBoxXY_toggled(bool checked)
{
	_viewportWidget->setXYClippingEnabled(checked);	
	_viewportWidget->updateClippingPlane();
	_viewportWidget->update();
}

void ClippingPlanesEditor::on_checkBoxYZ_toggled(bool checked)
{
	_viewportWidget->setYZClippingEnabled(checked);	
	_viewportWidget->updateClippingPlane();
	_viewportWidget->update();
}

void ClippingPlanesEditor::on_checkBoxZX_toggled(bool checked)
{	
	_viewportWidget->setZXClippingEnabled(checked);
	_viewportWidget->updateClippingPlane();
	_viewportWidget->update();
}

void ClippingPlanesEditor::on_checkBoxFlipXY_toggled(bool checked)
{
	_viewportWidget->setClippingZFlipped(checked);	
	_viewportWidget->updateClippingPlane();
	_viewportWidget->update();
}

void ClippingPlanesEditor::on_checkBoxFlipYZ_toggled(bool checked)
{
	_viewportWidget->setClippingXFlipped(checked);	
	_viewportWidget->updateClippingPlane();
	_viewportWidget->update();
}

void ClippingPlanesEditor::on_checkBoxFlipZX_toggled(bool checked)
{
	_viewportWidget->setClippingYFlipped(checked);
	_viewportWidget->updateClippingPlane();
	_viewportWidget->update();
}

void ClippingPlanesEditor::on_checkBoxCapping_toggled(bool checked)
{
	_viewportWidget->setCappingPlanesEnabled(checked);
	_viewportWidget->updateClippingPlane();
	_viewportWidget->update();
}

void ClippingPlanesEditor::on_checkBoxDynamicCapping_toggled(bool checked)
{
	_viewportWidget->setSectionCapsDynamicEnabled(checked);
}

void ClippingPlanesEditor::on_doubleSpinBoxXYCoeff_valueChanged(double val)
{
	_viewportWidget->setClippingZCoeff(val);
	_viewportWidget->updateClippingPlane();
	_viewportWidget->update();
}

void ClippingPlanesEditor::on_doubleSpinBoxYZCoeff_valueChanged(double val)
{
	_viewportWidget->setClippingXCoeff(val);	
	_viewportWidget->updateClippingPlane();
	_viewportWidget->update();
}

void ClippingPlanesEditor::on_doubleSpinBoxZXCoeff_valueChanged(double val)
{
	_viewportWidget->setClippingYCoeff(val);
	_viewportWidget->updateClippingPlane();
	_viewportWidget->update();
}

void ClippingPlanesEditor::on_pushButtonResetCoeffs_clicked()
{	
	doubleSpinBoxZXCoeff->setValue(0);
	doubleSpinBoxXYCoeff->setValue(0);
	doubleSpinBoxYZCoeff->setValue(0);
}

void ClippingPlanesEditor::on_radioButtonProcedural_toggled(bool checked)
{
	_viewportWidget->setClippingPlaneHatchMode(checked ? ClippingPlaneHatchMode::PROCEDURAL : ClippingPlaneHatchMode::TEXTURE);
	_viewportWidget->updateClippingPlane();
	_viewportWidget->update();
}

void ClippingPlanesEditor::on_comboBoxHatchMode_currentIndexChanged(int index)
{
	_viewportWidget->setClippingPlaneHatchPattern(static_cast<HatchPattern>(index));
	_viewportWidget->updateClippingPlane();
	_viewportWidget->update();
}

void ClippingPlanesEditor::on_spinBoxHatchTiling_valueChanged(int val)
{
	_viewportWidget->setHatchTiling(val);
	_viewportWidget->updateClippingPlane();
	_viewportWidget->update();
}

void ClippingPlanesEditor::on_doubleSpinBoxThickness_valueChanged(double val)
{
	_viewportWidget->setHatchLineThickness(static_cast<float>(val));
	_viewportWidget->updateClippingPlane();
	_viewportWidget->update();
}

void ClippingPlanesEditor::on_doubleSpinBoxIntensity_valueChanged(double val)
{
	_viewportWidget->setHatchIntensity(static_cast<float>(val));
	_viewportWidget->updateClippingPlane();
	_viewportWidget->update();
}

void ClippingPlanesEditor::on_pushButtonHatchColor_clicked()
{
	QColor color = QColorDialog::getColor(QColor(0,0,0), this, tr("Select Hatch Color"));
	if (color.isValid())
	{		
		pushButtonHatchColor->setStyleSheet(
			QString("background-color: %1; color: %2;")
			.arg(color.name())
			.arg(color.lightness() < 128 ? "#FFFFFF" : "#000000")
		);
		_viewportWidget->setHatchLineColor(color);
		_viewportWidget->updateClippingPlane();
		_viewportWidget->update();
	}
}

void ClippingPlanesEditor::on_pushButtonTexture_clicked()
{
	const QString path = PathUtils::getDataDirectory() + "/";
	QString filePath = QFileDialog::getOpenFileName(this, tr("Select Hatch Texture"), QString(path + "textures/patterns"),
		tr("Image Files (*.png *.jpg *.bmp)"));
	if (!filePath.isEmpty())
	{
		// make the button a fixed-size square thumbnail (say 48x48)
		int thumb = 140;
		pushButtonTexture->setFixedSize(thumb, thumb);

		// set icon and scale it to full button area
		QPixmap pix(filePath);
		QIcon icon(pix);
		pushButtonTexture->setIcon(icon);
		pushButtonTexture->setIconSize(pushButtonTexture->size());

		// remove text and optional focus/flat look
		pushButtonTexture->setText(QString());		

		// optionally use tooltip for the filename
		pushButtonTexture->setToolTip(QFileInfo(filePath).fileName());

		_viewportWidget->setHatchTexture(filePath);
		_viewportWidget->updateClippingPlane();
		_viewportWidget->update();
	}
	else
	{
		pushButtonTexture->setText(tr("Select Texture"));
	}
}

void ClippingPlanesEditor::on_pushButtonDefaultValues_clicked()
{
	resetProceduralTextureValues();
}

void ClippingPlanesEditor::on_pushButtonResetAll_clicked()
{
	// set default values
	doubleSpinBoxXYCoeff->setValue(0);
	doubleSpinBoxYZCoeff->setValue(0);
	doubleSpinBoxZXCoeff->setValue(0);
	checkBoxXY->setChecked(false);
	checkBoxYZ->setChecked(false);
	checkBoxZX->setChecked(false);
	checkBoxFlipXY->setChecked(false);
	checkBoxFlipYZ->setChecked(false);
	checkBoxFlipZX->setChecked(false);
	checkBoxCapping->setChecked(false);
	radioButtonProcedural->setChecked(true);
	resetProceduralTextureValues();
	pushButtonTexture->setText(tr("Select Texture"));
	pushButtonTexture->setIcon(QIcon());
}

void ClippingPlanesEditor::resetProceduralTextureValues()
{
	comboBoxHatchMode->setCurrentIndex(0);
	spinBoxHatchTiling->setValue(100);
	doubleSpinBoxThickness->setValue(0.05);
	doubleSpinBoxIntensity->setValue(1.0f);
	pushButtonHatchColor->setStyleSheet("background-color: #000000; color: #FFFFFF;");
	_viewportWidget->setHatchLineColor(QColor(0, 0, 0));
}
