#pragma once

#include <QMetaObject>
#include <QPointer>
#include <QUuid>
#include <QWidget>

#include <array>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLineEdit;
class QSpinBox;
class QTableWidget;
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
	void editPlotRequested(const QUuid& meshUuid);

private:
	// Every user-visible string of this panel - labels, group title, tooltips, suffixes AND the items of its combo boxes - is
	// assigned here and nowhere else, so the constructor and retranslate() cannot drift apart. Combo items keep their data and
	// the current selection; only their text is replaced.
	void applyTexts();
	void refreshState();
	void applyColourState();
	void applyAppearanceState();
	void applyBarAppearanceState();
	void applyReferencePlaneState();
	void applyAxisState();
	void applyTextLabels(); // the notes table -> the active plot
	void addTextLabel();      // at the centre of the plot's data (coordinates can then be typed)
	void placeTextLabel();    // arms the click tool: click a point on a plot
	void removeTextLabel();

	QPointer<ModelViewer> _viewer;
	QMetaObject::Connection _stateConnection;
	QPushButton* _addPlotButton = nullptr;
	QPushButton* _editPlotButton = nullptr;
	QComboBox* _plotSelector = nullptr;
	QCheckBox* _showAxesCheck = nullptr;
	QLineEdit* _plotTitle = nullptr;
	QComboBox* _colormap = nullptr;
	QComboBox* _bands = nullptr;
	QCheckBox* _automaticRange = nullptr;
	QLabel* _contourLevelsLabel = nullptr;
	QSpinBox* _contourLevels = nullptr;
	QCheckBox* _contourProjected = nullptr;
	QLabel* _contourOverlayLabel = nullptr;
	QComboBox* _contourOverlay = nullptr;
	QCheckBox* _sectionProbe = nullptr;
	QCheckBox* _pathlineAnimation = nullptr;
	QDoubleSpinBox* _rangeMinimum = nullptr;
	QDoubleSpinBox* _rangeMaximum = nullptr;
	QDoubleSpinBox* _lineWidth = nullptr;
	QDoubleSpinBox* _markerSize = nullptr;
	QDoubleSpinBox* _arrowScale = nullptr;
	QLabel* _barWidthScaleLabel = nullptr;
	QDoubleSpinBox* _barWidthScale = nullptr;
	QLabel* _barDepthScaleLabel = nullptr;
	QDoubleSpinBox* _barDepthScale = nullptr;
	std::array<QCheckBox*, 3> _referencePlanes{};
	QSpinBox* _referencePlaneOpacity = nullptr;
	std::array<QLineEdit*, 3> _axisLabels{};
	std::array<QComboBox*, 3> _axisScales{};
	std::array<QCheckBox*, 3> _axisAutomatic{};
	std::array<QDoubleSpinBox*, 3> _axisMinimum{};
	std::array<QDoubleSpinBox*, 3> _axisMaximum{};
	std::array<QSpinBox*, 3> _axisTicks{};
	QLabel* _axisStatus = nullptr;
	QGroupBox* _notesGroup = nullptr;
	QTableWidget* _notesTable = nullptr;
	QPushButton* _addNoteButton = nullptr;
	QPushButton* _placeNoteButton = nullptr;
	QPushButton* _removeNoteButton = nullptr;
	bool _applyingNotes = false; // the table is the source of the change in flight: refreshState() must not rebuild it

	// Labels that carry text applyTexts() must be able to reach again.
	QLabel* _activePlotLabel = nullptr;
	QLabel* _colormapLabel = nullptr;
	QLabel* _bandsLabel = nullptr;
	QLabel* _minimumLabel = nullptr;
	QLabel* _maximumLabel = nullptr;
	QLabel* _lineWidthLabel = nullptr;
	QLabel* _markerSizeLabel = nullptr;
	QLabel* _arrowScaleLabel = nullptr;
	QLabel* _titleLabel = nullptr;
	QLabel* _planesLabel = nullptr;
	QLabel* _opacityLabel = nullptr;
	QGroupBox* _axesGroup = nullptr;
};
