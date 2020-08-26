#include "SphericalHarmonicsEditor.h"
#include "ui_SphericalHarmonicsEditor.h"

#include "SphericalHarmonic.h"
#include "GLWidget.h"

SphericalHarmonicsEditor::SphericalHarmonicsEditor(SphericalHarmonic* sphere, QWidget* parent) :
	QWidget(parent),
	ui(new Ui::SphericalHarmonicsEditor),
	_sphere(sphere)
{
	ui->setupUi(this);

	ui->doubleSpinBoxM0->setValue(_sphere->_coeff1);
	ui->doubleSpinBoxM2->setValue(_sphere->_coeff2);
	ui->doubleSpinBoxM4->setValue(_sphere->_coeff3);
	ui->doubleSpinBoxM6->setValue(_sphere->_coeff4);
	ui->doubleSpinBoxM1->setValue(_sphere->_power1);
	ui->doubleSpinBoxM3->setValue(_sphere->_power2);
	ui->doubleSpinBoxM5->setValue(_sphere->_power3);
	ui->doubleSpinBoxM7->setValue(_sphere->_power4);
}

SphericalHarmonicsEditor::~SphericalHarmonicsEditor()
{
	delete ui;
}

void SphericalHarmonicsEditor::on_doubleSpinBoxM0_valueChanged(double val)
{
	_sphere->_coeff1 = val;
	_sphere->buildMesh();
    dynamic_cast<GLWidget*>(parent())->updateBoundingSphere();
}

void SphericalHarmonicsEditor::on_doubleSpinBoxM1_valueChanged(double val)
{
	_sphere->_power1 = val;
	_sphere->buildMesh();
    dynamic_cast<GLWidget*>(parent())->updateBoundingSphere();
}

void SphericalHarmonicsEditor::on_doubleSpinBoxM2_valueChanged(double val)
{
	_sphere->_coeff2 = val;
	_sphere->buildMesh();
    dynamic_cast<GLWidget*>(parent())->updateBoundingSphere();
}

void SphericalHarmonicsEditor::on_doubleSpinBoxM3_valueChanged(double val)
{
	_sphere->_power2 = val;
	_sphere->buildMesh();
    dynamic_cast<GLWidget*>(parent())->updateBoundingSphere();
}

void SphericalHarmonicsEditor::on_doubleSpinBoxM4_valueChanged(double val)
{
	_sphere->_coeff3 = val;
	_sphere->buildMesh();
    dynamic_cast<GLWidget*>(parent())->updateBoundingSphere();
}

void SphericalHarmonicsEditor::on_doubleSpinBoxM5_valueChanged(double val)
{
	_sphere->_power3 = val;
	_sphere->buildMesh();
    dynamic_cast<GLWidget*>(parent())->updateBoundingSphere();
}

void SphericalHarmonicsEditor::on_doubleSpinBoxM6_valueChanged(double val)
{
	_sphere->_coeff4 = val;
	_sphere->buildMesh();
    dynamic_cast<GLWidget*>(parent())->updateBoundingSphere();
}

void SphericalHarmonicsEditor::on_doubleSpinBoxM7_valueChanged(double val)
{
	_sphere->_power4 = val;
	_sphere->buildMesh();
    dynamic_cast<GLWidget*>(parent())->updateBoundingSphere();
}
