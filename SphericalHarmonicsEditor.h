#ifndef SPHERICALHARMONICSEDITOR_H
#define SPHERICALHARMONICSEDITOR_H

#include <QWidget>

namespace Ui {
class SphericalHarmonicsEditor;
}

class SphericalHarmonic;
class SphericalHarmonicsEditor : public QWidget
{
    Q_OBJECT

public:
    explicit SphericalHarmonicsEditor(SphericalHarmonic* sphere, QWidget *parent = nullptr);
    ~SphericalHarmonicsEditor();

protected slots:
	void on_doubleSpinBoxM0_valueChanged(double val);
	void on_doubleSpinBoxM1_valueChanged(double val);
	void on_doubleSpinBoxM2_valueChanged(double val);
	void on_doubleSpinBoxM3_valueChanged(double val);
	void on_doubleSpinBoxM4_valueChanged(double val);
	void on_doubleSpinBoxM5_valueChanged(double val);
	void on_doubleSpinBoxM6_valueChanged(double val);
	void on_doubleSpinBoxM7_valueChanged(double val);

private:
    Ui::SphericalHarmonicsEditor *ui;
	SphericalHarmonic* _sphere;
};

#endif // SPHERICALHARMONICSEDITOR_H
