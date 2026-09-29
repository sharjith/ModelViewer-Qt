#pragma once

#include <QMetaObject>
#include <QPointer>
#include <QWidget>

#include <array>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QSpinBox;
class QLabel;
class QPushButton;
class ModelViewer;

// Persistent 3D Plot tab in MainWindow's document dock. Plot creation remains a separate, roomy import dialog;
// this panel owns controls that must remain available after that dialog closes. It is a shared panel and is
// rebound to the active ModelViewer in the same way as SimulationPanel.
class Plot3DControlsPanel : public QWidget
{
	Q_OBJECT
public:
	explicit Plot3DControlsPanel(QWidget* parent = nullptr);
	void setModelViewer(ModelViewer* viewer);
	void retranslate();

signals:
	void addPlotRequested();

private:
	void refreshState();
	void applyColourState();
	void applyAppearanceState();
	void applyBarAppearanceState();
	void applyAxisState();

	QPointer<ModelViewer> _viewer;
	QMetaObject::Connection _stateConnection;
	QPushButton* _addPlotButton = nullptr;
	QComboBox* _plotSelector = nullptr;
	QCheckBox* _showAxesCheck = nullptr;
	QLineEdit* _plotTitle = nullptr;
	QComboBox* _colormap = nullptr;
	QComboBox* _bands = nullptr;
	QCheckBox* _automaticRange = nullptr;
	QLabel* _contourLevelsLabel = nullptr;
	QSpinBox* _contourLevels = nullptr;
	QDoubleSpinBox* _rangeMinimum = nullptr;
	QDoubleSpinBox* _rangeMaximum = nullptr;
	QDoubleSpinBox* _lineWidth = nullptr;
	QDoubleSpinBox* _markerSize = nullptr;
	QDoubleSpinBox* _arrowScale = nullptr;
	QLabel* _barWidthScaleLabel = nullptr;
	QDoubleSpinBox* _barWidthScale = nullptr;
	QLabel* _barDepthScaleLabel = nullptr;
	QDoubleSpinBox* _barDepthScale = nullptr;
	std::array<QLineEdit*, 3> _axisLabels{};
	std::array<QComboBox*, 3> _axisScales{};
	std::array<QCheckBox*, 3> _axisAutomatic{};
	std::array<QDoubleSpinBox*, 3> _axisMinimum{};
	std::array<QDoubleSpinBox*, 3> _axisMaximum{};
	std::array<QSpinBox*, 3> _axisTicks{};
	QLabel* _axisStatus = nullptr;
};
