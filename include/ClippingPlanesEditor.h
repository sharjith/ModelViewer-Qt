#pragma once


#include <QDialog>
#include "BoundingBox.h"
#include "ui_ClippingPlanesEditor.h"

class ViewportWidget;

// In-viewport overlay for the 3 clipping planes - which axes are active,
// where, flipped or not, capped or not, gizmo on/off. Cap-fill STYLE
// (Procedural vs. Textured mode, hatch pattern/tiling/thickness/intensity/
// line color) is deliberately NOT here - it moved to Settings -> Rendering
// -> Section Capping as a once-per-document default (seeded in
// ViewportWidget::createCappingPlanes()), since it's a "set once, rarely
// revisited" preference, not something touched during interactive work like
// everything else this panel hosts. The one exception is the texture
// PICKER itself (pushButtonTexture) - which specific image file to use is a
// per-use content choice, not a preference, so it stays here, shown only
// when the Settings-configured default mode is Textured (checked once at
// construction via ViewportWidget::clippingPlaneHatchMode() - mode itself
// is no longer a live in-panel toggle).
class ClippingPlanesEditor : public QWidget, Ui::ClippingPlanesEditor
{
	Q_OBJECT

public:
	explicit ClippingPlanesEditor(ViewportWidget* parent = nullptr);
	~ClippingPlanesEditor();

	void setCoefficientLimits(double xMin, double xMax, double yMin, double yMax, double zMin, double zMax);
	void applyContrastTheme(const QColor& textColor);
	void applyBackgroundTheme(const QColor& topColor, const QColor& bottomColor);

	// Whether the draggable plane gizmos should be shown in the viewport
	// (still further gated per-axis by that axis's own enable checkbox -
	// see ViewportWidget::updatePlaneGizmos()). Backed by checkBoxShowGizmo.
	bool isGizmoVisible() const;

	// Pushes a coefficient value into the matching spin box WITHOUT
	// re-triggering its own on_doubleSpinBox*Coeff_valueChanged() handler
	// (QSignalBlocker-guarded) - called from ViewportWidget's PlaneGizmo
	// drag callbacks so the numeric field stays live during a drag without
	// feeding back into another setClippingXCoeff() call for the same
	// value that drag already applied directly.
	void setXCoeffDisplay(double value);
	void setYCoeffDisplay(double value);
	void setZCoeffDisplay(double value);

	// Applies one of the toolbar flyout's presets: exactly this combination of the
	// three planes (or, if `box`, the box) on and everything else off. Goes through
	// the checkboxes so their own toggled handlers - mutual exclusion with Box,
	// render state, viewport update - run exactly as if the user had ticked them.
	// xy/yz/zx use this panel's own naming (XY = the Z-normal plane).
	void applyPreset(bool xy, bool yz, bool zx, bool box);

	// ---- Box clipping (4th mode) --------------------------------------------
	// Box limits are ABSOLUTE world coordinates (unlike the relative axis
	// coefficients above), so their spin boxes get their own range setter fed
	// from the scene's real min/max - not setCoefficientLimits()'s zero-centered
	// half-sizes, which would clamp a model located away from the origin.
	// Face order is 0..5 = xMin, xMax, yMin, yMax, zMin, zMax throughout.
	void setBoxLimitRanges(double xMin, double xMax, double yMin, double yMax, double zMin, double zMax);
	// Display-only pushes (QSignalBlocker-guarded, like the *CoeffDisplay setters
	// above): keep the spin boxes live during a gizmo drag / undo without
	// feeding back into setBoxClippingLimit() for the value that call just applied.
	void setBoxLimitDisplay(int face, double value);
	void setBoxLimitsDisplay(const BoundingBox& limits);

protected slots:
	void keyPressEvent(QKeyEvent* e);
	void on_checkBoxXY_toggled(bool checked);
	void on_checkBoxYZ_toggled(bool checked);
	void on_checkBoxZX_toggled(bool checked);
	void on_checkBoxFlipXY_toggled(bool checked);
	void on_checkBoxFlipYZ_toggled(bool checked);
	void on_checkBoxFlipZX_toggled(bool checked);
	void on_checkBoxCapping_toggled(bool checked);
	void on_checkBoxDynamicCapping_toggled(bool checked);
	void on_checkBoxShowGizmo_toggled(bool checked);
	void on_doubleSpinBoxXYCoeff_valueChanged(double val);
	void on_doubleSpinBoxYZCoeff_valueChanged(double val);
	void on_doubleSpinBoxZXCoeff_valueChanged(double val);
	void on_checkBoxBoxClip_toggled(bool checked);
	void on_checkBoxBoxKeepInside_toggled(bool checked);
	void on_doubleSpinBoxBoxXMin_valueChanged(double val);
	void on_doubleSpinBoxBoxXMax_valueChanged(double val);
	void on_doubleSpinBoxBoxYMin_valueChanged(double val);
	void on_doubleSpinBoxBoxYMax_valueChanged(double val);
	void on_doubleSpinBoxBoxZMin_valueChanged(double val);
	void on_doubleSpinBoxBoxZMax_valueChanged(double val);
	void on_pushButtonBoxReset_clicked();
	void on_pushButtonResetCoeffs_clicked();
	void on_pushButtonTexture_clicked();
	void on_pushButtonResetAll_clicked();

private:
	// The six box-limit spin boxes indexed by face (0..5 = xMin, xMax, yMin,
	// yMax, zMin, zMax) - one place to iterate them instead of six named uses.
	QDoubleSpinBox* boxSpin(int face) const;

	ViewportWidget* _viewportWidget;
};
