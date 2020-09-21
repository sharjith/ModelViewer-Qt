#include "SuperToroidEditor.h"
#include "SuperToroid.h"
#include "GLWidget.h"

SuperToroidEditor::SuperToroidEditor(SuperToroid* storoid, QWidget* parent)
	: QWidget(parent),
	_toroid(storoid)
{
	setupUi(this);

	doubleSpinBoxOutRad->setValue(_toroid->_outerRadius);
	doubleSpinBoxInnRad->setValue(_toroid->_innerRadius);
	doubleSpinBoxN1->setValue(_toroid->_n1);
	doubleSpinBoxN2->setValue(_toroid->_n2);
}

SuperToroidEditor::~SuperToroidEditor()
{
}

void SuperToroidEditor::on_doubleSpinBoxN1_valueChanged(double val)
{
	_toroid->_n1 = val;
	_toroid->buildMesh();
	dynamic_cast<GLWidget*>(parent())->updateBoundingSphere();
}

void SuperToroidEditor::on_doubleSpinBoxN2_valueChanged(double val)
{
	_toroid->_n2 = val;
	_toroid->buildMesh();
	dynamic_cast<GLWidget*>(parent())->updateBoundingSphere();
}

void SuperToroidEditor::on_doubleSpinBoxOutRad_valueChanged(double val)
{
	_toroid->_outerRadius = val;
	_toroid->buildMesh();
	dynamic_cast<GLWidget*>(parent())->updateBoundingSphere();
}

void SuperToroidEditor::on_doubleSpinBoxInnRad_valueChanged(double val)
{
	_toroid->_innerRadius = val;
	_toroid->buildMesh();
	dynamic_cast<GLWidget*>(parent())->updateBoundingSphere();
}