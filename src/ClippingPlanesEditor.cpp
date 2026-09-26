#include "ClippingPlanesEditor.h"
#include "ui_ClippingPlanesEditor.h"
#include "LanguageManager.h"
#include "ViewportWidget.h"
#include "PathUtils.h"
#include <QCheckBox>
#include <QKeyEvent>
#include <QFileDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QPainter>
#include <QProxyStyle>
#include <QStyleOptionButton>
#include <QSignalBlocker>
#include <QUrl>

#include <algorithm>

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

	// setupUi() applies the .ui file's "checked" default directly to the
	// widget, but connectSlotsByName() (which wires up on_checkBoxCapping_toggled()
	// etc. via Qt's auto-connect) only runs at the END of setupUi() - so a
	// checkbox that starts checked in the .ui never actually fires its
	// toggled() signal for that initial state, and checkBoxCapping's
	// underlying render-side flag (_renderCtrl._cappingEnabled, read by
	// drawSectionCapping()) stays at its own separate false default even
	// though the checkbox itself displays checked. Force them back in sync
	// here - trivial flag setters, safe to call before the viewport has
	// rendered anything yet. checkBoxShowGizmo needs no equivalent fix:
	// isGizmoVisible() reads checkBoxShowGizmo->isChecked() live on demand
	// rather than caching a synced copy, so it can't desync this way.
	_viewportWidget->setCappingPlanesEnabled(checkBoxCapping->isChecked());

	connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
		retranslateUi(this);
		});

	// Texture picker is only relevant when the Settings-configured default
	// mode is Textured - see this class's own header doc comment. Checked
	// once here (mode is no longer a live in-panel toggle), not re-checked
	// later, matching how other Settings-seeded per-document defaults
	// (e.g. up-axis) don't retroactively update an already-open document.
	pushButtonTexture->setVisible(_viewportWidget->clippingPlaneHatchMode() == ClippingPlaneHatchMode::TEXTURE);

	// The box's six limit fields + Reset only take space while Box mode is on, so
	// the panel is exactly as tall as before until the user opts in (it is a
	// bottom-anchored overlay with no scrolling - see ViewportWidget's
	// _lowerLayout).
	widgetBoxLimits->setVisible(checkBoxBoxClip->isChecked());

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
	pushButtonResetCoeffs->setStyleSheet(blackTextStyle);
	pushButtonResetAll->setStyleSheet(blackTextStyle);
	pushButtonBoxReset->setStyleSheet(blackTextStyle);
	pushButtonTexture->setStyleSheet(QStringLiteral("background-color: rgba(255, 255, 255, 5%); color: rgb(0, 0, 0);"));

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

bool ClippingPlanesEditor::isGizmoVisible() const
{
	return checkBoxShowGizmo->isChecked();
}

void ClippingPlanesEditor::applyCuts(const bool enabled[3], const double coefficient[3], const bool flipped[3])
{
	// The X-normal plane is the "YZ" row, the Y-normal one "XZ", the Z-normal one "XY" (see the *CoeffDisplay setters).
	QDoubleSpinBox* spins[3] = { doubleSpinBoxYZCoeff, doubleSpinBoxZXCoeff, doubleSpinBoxXYCoeff };
	QCheckBox* flips[3] = { checkBoxFlipYZ, checkBoxFlipZX, checkBoxFlipXY };
	QCheckBox* planes[3] = { checkBoxYZ, checkBoxZX, checkBoxXY };
	for (int axis = 0; axis < 3; ++axis)
	{
		if (!enabled[axis])
			continue;
		flips[axis]->setChecked(flipped[axis]);
		spins[axis]->setValue(coefficient[axis]);
		planes[axis]->setChecked(true);
	}
}

void ClippingPlanesEditor::setXCoeffDisplay(double value)
{
	const QSignalBlocker blocker(doubleSpinBoxYZCoeff); // X-normal plane is named "YZ" (the plane it spans)
	doubleSpinBoxYZCoeff->setValue(value);
}

void ClippingPlanesEditor::setYCoeffDisplay(double value)
{
	const QSignalBlocker blocker(doubleSpinBoxZXCoeff); // Y-normal plane is labelled "XZ"
	doubleSpinBoxZXCoeff->setValue(value);
}

void ClippingPlanesEditor::setZCoeffDisplay(double value)
{
	const QSignalBlocker blocker(doubleSpinBoxXYCoeff); // Z-normal plane is named "XY"
	doubleSpinBoxXYCoeff->setValue(value);
}

void ClippingPlanesEditor::on_checkBoxShowGizmo_toggled(bool /*checked*/)
{
	_viewportWidget->updatePlaneGizmos();
	_viewportWidget->updateClipBoxGizmos();
	_viewportWidget->update();
}

void ClippingPlanesEditor::applyPreset(bool xy, bool yz, bool zx, bool box)
{
	if (box)
	{
		// Ticking Box unticks the three planes itself (see on_checkBoxBoxClip_toggled()).
		checkBoxBoxClip->setChecked(true);
		return;
	}

	// Box off first, then the planes: an axis handler would also untick Box when it
	// is newly ticked, but doing it explicitly keeps this independent of that.
	checkBoxBoxClip->setChecked(false);
	checkBoxXY->setChecked(xy);
	checkBoxYZ->setChecked(yz);
	checkBoxZX->setChecked(zx);
}

QDoubleSpinBox* ClippingPlanesEditor::boxSpin(int face) const
{
	switch (face)
	{
	case 0: return doubleSpinBoxBoxXMin;
	case 1: return doubleSpinBoxBoxXMax;
	case 2: return doubleSpinBoxBoxYMin;
	case 3: return doubleSpinBoxBoxYMax;
	case 4: return doubleSpinBoxBoxZMin;
	default: return doubleSpinBoxBoxZMax;
	}
}

void ClippingPlanesEditor::setBoxLimitRanges(double xMin, double xMax, double yMin, double yMax, double zMin, double zMax)
{
	const double lo[3] = { xMin, yMin, zMin };
	const double hi[3] = { xMax, yMax, zMax };
	for (int face = 0; face < 6; ++face)
	{
		QDoubleSpinBox* spin = boxSpin(face);
		const int axis = face / 2;
		// Blocked: setRange() can clamp the current value and would otherwise emit
		// valueChanged -> setBoxClippingLimit() from what is only a range update.
		const QSignalBlocker blocker(spin);
		spin->setRange(lo[axis], hi[axis]);
		spin->setSingleStep(std::max((hi[axis] - lo[axis]) / 50.0, 1.0e-3));
		// setRange() may have clamped the displayed value (e.g. the scene moved
		// to a distant region) while the STORED limit is unchanged, leaving field
		// and render state out of sync. Re-show the stored limit; if it no longer
		// fits the new range the caller (ViewportWidget::updateClippingPlane())
		// re-seeds the box, which then updates both together.
		spin->setValue(_viewportWidget->boxClippingLimit(face));
	}
}

void ClippingPlanesEditor::setBoxLimitDisplay(int face, double value)
{
	if (face < 0 || face > 5)
		return;
	QDoubleSpinBox* spin = boxSpin(face);
	const QSignalBlocker blocker(spin);
	spin->setValue(value);
}

void ClippingPlanesEditor::setBoxLimitsDisplay(const BoundingBox& limits)
{
	setBoxLimitDisplay(0, limits.xMin());
	setBoxLimitDisplay(1, limits.xMax());
	setBoxLimitDisplay(2, limits.yMin());
	setBoxLimitDisplay(3, limits.yMax());
	setBoxLimitDisplay(4, limits.zMin());
	setBoxLimitDisplay(5, limits.zMax());
}

void ClippingPlanesEditor::on_checkBoxBoxClip_toggled(bool checked)
{
	// Box mode and the three per-axis planes are mutually exclusive (union-notch
	// and intersection-box semantics can't meaningfully combine). Unchecking the
	// axis boxes lets their own toggled handlers run, so the render state follows.
	if (checked)
	{
		checkBoxXY->setChecked(false);
		checkBoxYZ->setChecked(false);
		checkBoxZX->setChecked(false);
	}
	widgetBoxLimits->setVisible(checked);
	_viewportWidget->setBoxClippingEnabled(checked);
	_viewportWidget->updateClippingPlane();
	_viewportWidget->update();
}

void ClippingPlanesEditor::on_checkBoxBoxKeepInside_toggled(bool checked)
{
	// Unchecked (default): keep the outside, cut a box-shaped hole. Checked: keep
	// only the inside (crop to the box).
	_viewportWidget->setBoxClippingKeepInside(checked);
	_viewportWidget->update();
}

void ClippingPlanesEditor::on_doubleSpinBoxBoxXMin_valueChanged(double val) { _viewportWidget->setBoxClippingLimit(0, val); }
void ClippingPlanesEditor::on_doubleSpinBoxBoxXMax_valueChanged(double val) { _viewportWidget->setBoxClippingLimit(1, val); }
void ClippingPlanesEditor::on_doubleSpinBoxBoxYMin_valueChanged(double val) { _viewportWidget->setBoxClippingLimit(2, val); }
void ClippingPlanesEditor::on_doubleSpinBoxBoxYMax_valueChanged(double val) { _viewportWidget->setBoxClippingLimit(3, val); }
void ClippingPlanesEditor::on_doubleSpinBoxBoxZMin_valueChanged(double val) { _viewportWidget->setBoxClippingLimit(4, val); }
void ClippingPlanesEditor::on_doubleSpinBoxBoxZMax_valueChanged(double val) { _viewportWidget->setBoxClippingLimit(5, val); }

void ClippingPlanesEditor::on_pushButtonBoxReset_clicked()
{
	_viewportWidget->resetBoxClippingLimits();
}

void ClippingPlanesEditor::keyPressEvent(QKeyEvent* e)
{
	if (e->key() != Qt::Key_Escape)
		QWidget::keyPressEvent(e);
	else {/* minimize */ }
}

void ClippingPlanesEditor::on_checkBoxXY_toggled(bool checked)
{
	// Mutually exclusive with Box mode - see on_checkBoxBoxClip_toggled().
	if (checked && checkBoxBoxClip->isChecked())
		checkBoxBoxClip->setChecked(false);
	_viewportWidget->setXYClippingEnabled(checked);
	_viewportWidget->updateClippingPlane();
	_viewportWidget->update();
}

void ClippingPlanesEditor::on_checkBoxYZ_toggled(bool checked)
{
	if (checked && checkBoxBoxClip->isChecked())
		checkBoxBoxClip->setChecked(false);
	_viewportWidget->setYZClippingEnabled(checked);
	_viewportWidget->updateClippingPlane();
	_viewportWidget->update();
}

void ClippingPlanesEditor::on_checkBoxZX_toggled(bool checked)
{
	if (checked && checkBoxBoxClip->isChecked())
		checkBoxBoxClip->setChecked(false);
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
	// Box mode: off, back to the default keep-outside (hole), and re-seeded so its
	// next enable starts from a fresh default box (its toggled handler hides the
	// limit fields again).
	checkBoxBoxKeepInside->setChecked(false);
	checkBoxBoxClip->setChecked(false);
	_viewportWidget->resetBoxClippingLimits();
	// Capping and Show Gizmo both default to checked now (see
	// ClippingPlanesEditor.ui's own "checked" properties) - this button's
	// own tooltip promises "Reset every clipping plane setting to its
	// default", so it needs to reset TO that, not to the stale false
	// defaults from before that change (a real bug: capping silently
	// stayed off after a reset even though a brand new panel starts with
	// it on).
	checkBoxCapping->setChecked(true);
	checkBoxShowGizmo->setChecked(true);
	// Mode/pattern/tiling/thickness/intensity/line color are no longer
	// per-document state (see this class's own header doc comment) - only
	// the texture PICK itself (which file, if any) resets here.
	pushButtonTexture->setText(tr("Select Texture"));
	pushButtonTexture->setIcon(QIcon());
}
