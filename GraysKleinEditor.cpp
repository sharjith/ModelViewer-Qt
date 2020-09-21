#include "GraysKleinEditor.h"
#include "ui_GraysKleinEditor.h"
#include "GraysKlein.h"
#include "GLWidget.h"

GraysKleinEditor::GraysKleinEditor(GraysKlein* klein, QWidget* parent)
	: QWidget(parent),
	ui(new Ui::GraysKleinEditor()),
	_graysKlein(klein)
{
	ui->setupUi(this);

	ui->doubleSpinBoxA->setValue(_graysKlein->_A);
	ui->doubleSpinBoxM->setValue(_graysKlein->_M);
	ui->doubleSpinBoxN->setValue(_graysKlein->_N);
}

GraysKleinEditor::~GraysKleinEditor()
{
	delete ui;
}

void GraysKleinEditor::on_doubleSpinBoxA_valueChanged(double val)
{
	_graysKlein->_A = val;
	_graysKlein->buildMesh();
	dynamic_cast<GLWidget*>(parent())->updateBoundingSphere();
}

void GraysKleinEditor::on_doubleSpinBoxM_valueChanged(double val)
{
	_graysKlein->_M = val;
	_graysKlein->buildMesh();
	dynamic_cast<GLWidget*>(parent())->updateBoundingSphere();
}

void GraysKleinEditor::on_doubleSpinBoxN_valueChanged(double val)
{
	_graysKlein->_N = val;
	_graysKlein->buildMesh();
	dynamic_cast<GLWidget*>(parent())->updateBoundingSphere();
}