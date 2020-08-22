#pragma once

#include <QWidget>
namespace Ui { class SpringEditor; };

class Spring;
class SpringEditor : public QWidget
{
	Q_OBJECT

public:
	SpringEditor(Spring* spring, QWidget* parent = Q_NULLPTR);
	~SpringEditor();

protected slots:
	void on_doubleSpinBoxSecRad_valueChanged(double val);
	void on_doubleSpinBoxCoilRad_valueChanged(double val);
	void on_doubleSpinBoxPitch_valueChanged(double val);
	void on_doubleSpinBoxTurns_valueChanged(double val);

private:
	Ui::SpringEditor* ui;

	Spring* _spring;
};
