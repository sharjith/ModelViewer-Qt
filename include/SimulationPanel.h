#pragma once

#include "SimulationResultDisplay.h"

#include <QWidget>

#include <memory>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QStackedWidget;

// The "Simulation" tab of the Document dock (bottom group, next to Selections/States) - see
// docs/simulation_results_design.md section 9.
//
// A single shared instance owned by MainWindow, like the other dock panels: it holds no document pointers.
// MainWindow feeds it the active document's active SimulationSession through setSession() and routes its signals
// back to that document (ModelViewer::openSimulationResult() / applySimulationViewState()). With no session it
// shows an empty state with an "Open Result..." button.
//
// The panel only edits a SimulationViewState (field, component, range, colormap, contours); ModelViewer applies it
// to the result mesh and the legend, then feeds the updated session back, which is what keeps the automatic-range
// display current.
class SimulationPanel : public QWidget
{
	Q_OBJECT
public:
	explicit SimulationPanel(QWidget* parent = nullptr);

	// nullptr shows the empty state. Never emits viewStateChanged().
	void setSession(const SimulationSession* session);

signals:
	void openRequested();
	void viewStateChanged(const SimulationViewState& state);

private:
	void buildUi();
	void populateFields(int selectedFieldIndex);
	void populateComponents(int fieldIndex, int selectedComponent);
	void refreshRangeEdits();
	// Shows [lo, hi] in the two spin boxes (decimals chosen from the span); `outward` rounds lo down and hi up so a
	// custom range prefilled from the data range never clips the data. Never emits.
	void setRangeDisplay(double lo, double hi, bool custom, bool outward);
	SimulationViewState currentState() const;
	void emitState();

	void onFieldChanged();
	void onRangeModeChanged();

	QStackedWidget* _stack = nullptr;
	QLabel* _fileLabel = nullptr;
	QLabel* _infoLabel = nullptr;
	QLabel* _noteLabel = nullptr;
	QComboBox* _fieldCombo = nullptr;
	QLabel* _componentLabel = nullptr;
	QComboBox* _componentCombo = nullptr;
	QComboBox* _rangeModeCombo = nullptr;
	QDoubleSpinBox* _minSpin = nullptr;
	QDoubleSpinBox* _maxSpin = nullptr;
	QComboBox* _colormapCombo = nullptr;
	QComboBox* _bandsCombo = nullptr;

	std::shared_ptr<ResultDataset> _dataset;
	bool _updating = false; // true while the controls are being filled from a session (suppresses signals)
};
