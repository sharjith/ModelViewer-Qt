#pragma once

#include <QMetaObject>
#include <QPointer>
#include <QWidget>

class QCheckBox;
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
	void refreshAxisState();

	QPointer<ModelViewer> _viewer;
	QMetaObject::Connection _axisStateConnection;
	QPushButton* _addPlotButton = nullptr;
	QCheckBox* _showAxesCheck = nullptr;
	QLabel* _axisStatus = nullptr;
};
