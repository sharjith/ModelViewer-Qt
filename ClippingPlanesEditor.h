#ifndef CLIPPINGPLANESEDITOR_H
#define CLIPPINGPLANESEDITOR_H

#include <QDialog>

namespace Ui {
	class ClippingPlanesEditor;
}
class GLWidget;
class ClippingPlanesEditor : public QWidget
{
	Q_OBJECT

public:
	explicit ClippingPlanesEditor(QWidget* parent = nullptr);
	~ClippingPlanesEditor();

protected slots:
	void keyPressEvent(QKeyEvent* e);
	void on_checkBoxXY_toggled(bool checked);
	void on_checkBoxYZ_toggled(bool checked);
	void on_checkBoxZX_toggled(bool checked);
	void on_checkBoxFlipXY_toggled(bool checked);
	void on_checkBoxFlipYZ_toggled(bool checked);
	void on_checkBoxFlipZX_toggled(bool checked);
	void on_doubleSpinBoxXYCoeff_valueChanged(double val);
	void on_doubleSpinBoxYZCoeff_valueChanged(double val);
	void on_doubleSpinBoxZXCoeff_valueChanged(double val);

private slots:
	void on_doubleSpinBoxDX_valueChanged(double arg1);
	void on_doubleSpinBoxDY_valueChanged(double arg1);
	void on_doubleSpinBoxDZ_valueChanged(double arg1);

private:
	Ui::ClippingPlanesEditor* ui;

	GLWidget* _glView;
};

#endif // CLIPPINGPLANESEDITOR_H
