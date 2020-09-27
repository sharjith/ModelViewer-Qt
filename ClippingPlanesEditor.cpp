#include "ClippingPlanesEditor.h"
#include "ui_ClippingPlanesEditor.h"

#include "GLWidget.h"

#include <QKeyEvent>

ClippingPlanesEditor::ClippingPlanesEditor(QWidget* parent) :
	QWidget(parent),
	ui(new Ui::ClippingPlanesEditor),
	_glView(dynamic_cast<GLWidget*>(parent))
{
	ui->setupUi(this);
}

ClippingPlanesEditor::~ClippingPlanesEditor()
{
	delete ui;
}

void ClippingPlanesEditor::keyPressEvent(QKeyEvent* e)
{
	if (e->key() != Qt::Key_Escape)
		QWidget::keyPressEvent(e);
	else {/* minimize */ }
}

void ClippingPlanesEditor::on_checkBoxXY_toggled(bool checked)
{
	_glView->_clipXEnabled = checked;
    _glView->updateClippingPlane();
	_glView->update();
}

void ClippingPlanesEditor::on_checkBoxYZ_toggled(bool checked)
{
	_glView->_clipYEnabled = checked;
    _glView->updateClippingPlane();
	_glView->update();
}

void ClippingPlanesEditor::on_checkBoxZX_toggled(bool checked)
{
	_glView->_clipZEnabled = checked;
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
