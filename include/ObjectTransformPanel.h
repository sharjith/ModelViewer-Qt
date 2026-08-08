#pragma once

#include <QWidget>
#include <memory>
#include <QVector3D>

namespace Ui {
	class ObjectTransformPanel;
}

class ObjectTransformPanel : public QWidget
{
	Q_OBJECT
public:
	explicit ObjectTransformPanel(QWidget* parent = nullptr);
	~ObjectTransformPanel();

	// Transformation data accessors
	QVector3D getTranslation() const;
	QVector3D getRotation() const;
	QVector3D getScale() const;

	// Update UI from data
	void setTranslationValues(const QVector3D& trans);
	void setRotationValues(const QVector3D& rot);
	void setScaleValues(const QVector3D& scale);

	// Reset all to defaults
	void resetAllValues();

	// Enable/disable controls
	void setControlsEnabled(bool enabled);

signals:
	void applyTransformationsRequested();
	void resetTransformationsRequested();

private slots:
	void onApplyButtonClicked();
	void onResetButtonClicked();

private:
	std::unique_ptr<Ui::ObjectTransformPanel> ui;
};
