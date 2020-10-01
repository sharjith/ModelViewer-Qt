#include "ClippingPlanesEditor.h"
#include "ui_ClippingPlanesEditor.h"

#include "GLWidget.h"

#include <QKeyEvent>

ClippingPlanesEditor::ClippingPlanesEditor(QWidget* parent) :
	QWidget(parent),
	_glView(dynamic_cast<GLWidget*>(parent))
{
	setupUi(this);
}

ClippingPlanesEditor::~ClippingPlanesEditor()
{
}

void ClippingPlanesEditor::keyPressEvent(QKeyEvent* e)
{
	if (e->key() != Qt::Key_Escape)
		QWidget::keyPressEvent(e);
	else {/* minimize */ }
}

void ClippingPlanesEditor::on_checkBoxXY_toggled(bool checked)
{
	_glView->_clipYZEnabled = checked;
	_glView->updateClippingPlane();
	_glView->update();
}

void ClippingPlanesEditor::on_checkBoxYZ_toggled(bool checked)
{
	_glView->_clipZXEnabled = checked;
	_glView->updateClippingPlane();
	_glView->update();
}

void ClippingPlanesEditor::on_checkBoxZX_toggled(bool checked)
{
	_glView->_clipXYEnabled = checked;
	_glView->updateClippingPlane();
	_glView->update();
}

void ClippingPlanesEditor::on_checkBoxFlipXY_toggled(bool checked)
{
	_glView->_clipXFlipped = checked;
	_glView->updateClippingPlane();
	_glView->update();
}

void ClippingPlanesEditor::on_checkBoxFlipYZ_toggled(bool checked)
{
	_glView->_clipYFlipped = checked;
	_glView->updateClippingPlane();
	_glView->update();
}

void ClippingPlanesEditor::on_checkBoxFlipZX_toggled(bool checked)
{
	_glView->_clipZFlipped = checked;
	_glView->updateClippingPlane();
	_glView->update();
}

void ClippingPlanesEditor::on_checkBoxCapping_toggled(bool checked)
{
	_glView->_cappingEnabled = checked;
	_glView->updateClippingPlane();
	_glView->update();
}

void ClippingPlanesEditor::on_doubleSpinBoxXYCoeff_valueChanged(double val)
{
	_glView->_clipXCoeff = val;
	_glView->updateClippingPlane();
	_glView->update();
}

void ClippingPlanesEditor::on_doubleSpinBoxYZCoeff_valueChanged(double val)
{
	_glView->_clipYCoeff = val;
	_glView->updateClippingPlane();
	_glView->update();
}

void ClippingPlanesEditor::on_doubleSpinBoxZXCoeff_valueChanged(double val)
{
	_glView->_clipZCoeff = val;
	_glView->updateClippingPlane();
	_glView->update();
}

void ClippingPlanesEditor::on_doubleSpinBoxDX_valueChanged(double arg1)
{
	_glView->_clipDX = arg1;
	_glView->updateClippingPlane();
	_glView->update();
}

void ClippingPlanesEditor::on_doubleSpinBoxDY_valueChanged(double arg1)
{
	_glView->_clipDY = arg1;
	_glView->updateClippingPlane();
	_glView->update();
}

void ClippingPlanesEditor::on_doubleSpinBoxDZ_valueChanged(double arg1)
{
	_glView->_clipDZ = arg1;
	_glView->updateClippingPlane();
	_glView->update();
}

void ClippingPlanesEditor::on_pushButtonResetCoeffs_clicked()
{
	doubleSpinBoxDX->setValue(0);
	doubleSpinBoxDY->setValue(0);
	doubleSpinBoxDZ->setValue(0);
	doubleSpinBoxZXCoeff->setValue(0);
	doubleSpinBoxXYCoeff->setValue(0);
	doubleSpinBoxYZCoeff->setValue(0);
}