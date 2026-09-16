#pragma once


#include <QDialog>
#include "ui_ClippingPlanesEditor.h"

class ViewportWidget;
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
	void on_pushButtonResetCoeffs_clicked();
	void on_radioButtonProcedural_toggled(bool checked);
	void on_comboBoxHatchMode_currentIndexChanged(int index);
	void on_spinBoxHatchTiling_valueChanged(int val);
	void on_doubleSpinBoxThickness_valueChanged(double val);
	void on_doubleSpinBoxIntensity_valueChanged(double val);
	void on_pushButtonHatchColor_clicked();
	void on_pushButtonTexture_clicked();
	void on_pushButtonDefaultValues_clicked();	
	void on_pushButtonResetAll_clicked();

private:
	void resetProceduralTextureValues();

private:
	ViewportWidget* _viewportWidget;
};
