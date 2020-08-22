#pragma once

#include <QDialog>
#include "ui_SuperToroidEditor.h"

class SuperToroid;
class SuperToroidEditor : public QWidget, public Ui::SuperToroidEditor
{
	Q_OBJECT

public:
	SuperToroidEditor(SuperToroid* storoid, QWidget* parent = Q_NULLPTR);
	~SuperToroidEditor();

protected slots:
	void on_doubleSpinBoxN1_valueChanged(double val);
	void on_doubleSpinBoxN2_valueChanged(double val);
	void on_doubleSpinBoxOutRad_valueChanged(double val);
	void on_doubleSpinBoxInnRad_valueChanged(double val);

private:
	SuperToroid* _toroid;
};
