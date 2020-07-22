#pragma once

#include <QWidget>
namespace Ui { class GraysKleinEditor; };

class GraysKlein;
class GraysKleinEditor : public QWidget
{
	Q_OBJECT

public:
	GraysKleinEditor(GraysKlein* klein, QWidget *parent = Q_NULLPTR);
	~GraysKleinEditor();

protected slots:
	void on_doubleSpinBoxA_valueChanged(double val);
	void on_doubleSpinBoxM_valueChanged(double val);
	void on_doubleSpinBoxN_valueChanged(double val);

private:
	Ui::GraysKleinEditor *ui;
	GraysKlein* _graysKlein;
};
