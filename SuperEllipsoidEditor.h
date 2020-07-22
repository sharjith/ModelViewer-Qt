#pragma once

#include <QWidget>
namespace Ui { class SuperEllipsoidEditor; };

class SuperEllipsoid;
class SuperEllipsoidEditor : public QWidget
{
	Q_OBJECT

public:
	SuperEllipsoidEditor(SuperEllipsoid* sellipsoid, QWidget *parent = Q_NULLPTR);
	~SuperEllipsoidEditor();

protected slots:
	void on_doubleSpinBoxScaleX_valueChanged(double val);
	void on_doubleSpinBoxScaleY_valueChanged(double val);
	void on_doubleSpinBoxScaleZ_valueChanged(double val);
	void on_doubleSpinBoxN1_valueChanged(double val);
	void on_doubleSpinBoxN2_valueChanged(double val);	
	void on_doubleSpinBoxRad_valueChanged(double val);

private:
	SuperEllipsoid* _ellipsoid;
	Ui::SuperEllipsoidEditor *ui;
};
