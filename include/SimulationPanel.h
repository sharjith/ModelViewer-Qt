#pragma once

#include "SimulationResultDisplay.h"

#include <QVector>
#include <QWidget>

#include <memory>

class QCheckBox;
class QToolButton;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
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
	// The document's results for the selector row (call before setSession); never emits.
	void setResults(const QVector<SimulationResultItem>& items, const QUuid& activeMeshUuid);
	// Whether compare mode is on and its options; also enables the Compare controls (needs a second result).
	void setCompareState(bool active, bool stacked, bool sharedRange);

signals:
	void openRequested();
	void resultActivated(const QUuid& meshUuid);
	void resultVisibilityChanged(const QUuid& meshUuid, bool visible);
	void resultCloseRequested(const QUuid& meshUuid);
	void compareStartRequested(const QUuid& otherMeshUuid, bool stacked, bool sharedRange);
	void compareStopRequested();
	void compareOptionsChanged(bool stacked, bool sharedRange);
	void viewStateChanged(const SimulationViewState& state);
	// The user changed the quantity or a unit of a field (empty strings = "not specified").
	void unitsChanged(int fieldIndex, const QString& kindId, const QString& fileUnit, const QString& displayUnit);

private:
	void buildUi();
	void populateFields(int selectedFieldIndex);
	void populateComponents(int fieldIndex, int selectedComponent);
	void populateGlyphFields(int selectedFieldIndex);
	void updateGlyphEnabled();
	void refreshRangeEdits();
	// Range mode of the combo: 0 = automatic over all steps, 1 = automatic for the step shown (also the only
	// automatic mode of a single-step result), 2 = custom.
	int rangeMode() const;
	void populateRangeModes(bool multiStep, const SimulationViewState& state);
	// The data range of the current field/component: over all steps, or for the step shown.
	bool currentDataRange(bool allSteps, float& lo, float& hi) const;
	// Shows [lo, hi] in the two spin boxes (decimals chosen from the span); `outward` rounds lo down and hi up so a
	// custom range prefilled from the data range never clips the data. Never emits.
	void setRangeDisplay(double lo, double hi, bool custom, bool outward);
	SimulationViewState currentState() const;
	void emitState();

	void onFieldChanged();
	void onRangeModeChanged();

	// Units of the current field (see ResultUnits.h). populateUnits() fills the three combos and labels from the
	// dataset without emitting; the handlers below emit unitsChanged().
	void populateUnits(int fieldIndex);
	void populateUnitCombos(const QString& kindId, const QString& fileUnit, const QString& displayUnit);
	void updateUnitLabels(int fieldIndex);
	void onKindEdited();
	void onUnitEdited();

	QStackedWidget* _stack = nullptr;
	QComboBox* _resultCombo = nullptr;
	QCheckBox* _resultVisibleCheck = nullptr;
	QToolButton* _resultCloseButton = nullptr;
	QComboBox* _compareCombo = nullptr;
	QPushButton* _compareButton = nullptr;
	QCheckBox* _compareStackedCheck = nullptr;
	QCheckBox* _compareSharedCheck = nullptr;
	QCheckBox* _compareLinkCheck = nullptr;
	bool _compareActive = false;
	QLabel* _fileLabel = nullptr;
	QLabel* _infoLabel = nullptr;
	QLabel* _noteLabel = nullptr;
	QComboBox* _fieldCombo = nullptr;
	QLabel* _componentLabel = nullptr;
	QComboBox* _componentCombo = nullptr;
	QComboBox* _rangeModeCombo = nullptr;
	QComboBox* _kindCombo = nullptr;
	QComboBox* _fileUnitCombo = nullptr;
	QComboBox* _displayUnitCombo = nullptr;
	QLabel* _unitStatusLabel = nullptr;
	QLabel* _minLabel = nullptr;
	QLabel* _maxLabel = nullptr;
	QDoubleSpinBox* _minSpin = nullptr;
	QDoubleSpinBox* _maxSpin = nullptr;
	QComboBox* _colormapCombo = nullptr;
	QComboBox* _bandsCombo = nullptr;
	QCheckBox* _deformCheck = nullptr;
	QCheckBox* _markersCheck = nullptr;
	QDoubleSpinBox* _deformScaleSpin = nullptr;
	QPushButton* _deformAutoButton = nullptr;
	QLabel* _deformInfoLabel = nullptr;
	double _autoDeformScale = 1.0;
	QCheckBox* _glyphCheck = nullptr;
	QComboBox* _glyphFieldCombo = nullptr;
	QDoubleSpinBox* _glyphScaleSpin = nullptr;
	QSpinBox* _glyphCountSpin = nullptr;
	QCheckBox* _glyphMagnitudeCheck = nullptr;
	QLabel* _glyphInfoLabel = nullptr;

	std::shared_ptr<ResultDataset> _dataset;
	int _step = 0;                // the step the session is at (for the automatic per-step range display)
	bool _lastAutoAllSteps = true; // which automatic mode was showing, so switching to Custom starts from it
	bool _updating = false; // true while the controls are being filled from a session (suppresses signals)
};
