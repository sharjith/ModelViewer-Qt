#pragma once

#include <QDialog>
#include <QVector3D>
#include <QSet>
#include <QHash>
#include <vector>

#include "MeshSurfaceAnchor.h"

#include "SurfaceAnalysisOverlay.h"
#include "WallThicknessAnalyzer.h"

class QToolButton;
class QButtonGroup;
class QStackedWidget;
class QLabel;
class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QSpinBox;
class QPushButton;
class QCloseEvent;
class QTimer;
class QMdiSubWindow;
class ModelViewer;
class MeshSelectionBox;
class NotesListBox;
class SceneMesh;
class AnalysisComputeSession;

// ---------------------------------------------------------------------------
// SurfaceAnalysisDialog (Tools -> Surface Analysis...)
//
// Unified dialog for curvature/wall-thickness/deviation inspection - one
// mode selector (3-way exclusive icon toggle-button group - checkable
// QToolButtons in a QButtonGroup, not QRadioButtons - since there are only
// 3 modes and showing all of them at once, as icon-forward switchable
// buttons, beats hiding them inside a closed combo box), one shared legend widget,
// one Apply/Clear pair - built on the Step 3 overlay foundation
// (RenderableMesh::setAnalysisOverlayColors()/setAnalysisOverlayFlatColors(),
// AnalysisColorRamp, SurfaceAnalysisOverlay).
//
// As of this step, all five sub-modes are implemented: Draft Angle + true
// ray-based Wall-Thickness (both on the Wall-Thickness panel), Zebra Stripe +
// mean-curvature colormap (both on the Curvature panel), and unsigned
// Deviation (its own panel) - see this app's own implementation plan for the
// build sequence. Gaussian/principal curvature modes remain a documented
// follow-up to the mean-curvature colormap already shipped here.
//
// Non-modal, per-document singleton (ModelViewer::openSurfaceAnalysisDialog()
// findChild-reuses it), matching MeasurementDialog/ShrinkWrapDialog/
// SubdivisionDialog's convention - this is an ongoing interactive tool the
// user keeps open alongside the viewport, not a one-shot modal report like
// MassPropertiesDialog/BatchRenderViewsDialog. Closing the dialog DOES clear
// every overlay it ever applied (all of SurfaceAnalysisOverlay's tracked
// entries, plus zebra-stripe on every mesh it was toggled on for - not just
// whatever happens to be selected at close time) - the dialog has
// WA_DeleteOnClose, so once it closes there is no "Clear Overlay" button
// left to press; leaving a heatmap/zebra-stripe permanently stuck on the
// mesh with no in-app way to turn it off would be worse than always
// clearing on close.
//
// Pure C++ widget construction (no .ui file), matching BatchRenderViewsDialog/
// MassPropertiesDialog's convention from this same session.
// ---------------------------------------------------------------------------
class SurfaceAnalysisDialog : public QDialog
{
	Q_OBJECT
public:
	explicit SurfaceAnalysisDialog(ModelViewer* modelViewer, QWidget* parent = nullptr);
	bool isComputationInFlight() const { return _activeSession != nullptr; }
	void requestComputationCancel();

	// Re-seeds the list of meshes from the viewer's current selection (used when the tool is invoked again while
	// this dialog is already open). Does nothing if nothing is selected in the viewer.
	void seedFromViewportSelection();
    void selectMode(const QString& mode);

	// True only while this dialog is open AND its "Show Readout on Hover"
	// toggle is checked - ViewportWidget checks this before doing any
	// picking work on mouse-move, so hovering costs nothing when the
	// feature isn't in use.
	bool hoverReadoutEnabled() const;

	// Formats the scalar value at `anchor` (from SelectionManager::
	// pickSurfaceAnchor(), the same hover-picking already driving
	// Measurement/Annotation hover previews) as "<Mode>: <value>[unit]" for
	// display near the cursor - empty string if `anchor` doesn't resolve to
	// a mesh with an active overlay, or the sample there is invalid. Draft
	// Angle is detected via SurfaceAnalysisOverlay::scalarAt()'s own
	// outIsFlat (it's not one of Mode's 3 values - see currentMode()'s own
	// doc comment); every other case is labeled from currentMode(), the
	// SAME page/mode the legend currently reflects, so the readout and
	// legend never disagree - including the pre-existing convention (not
	// introduced by this) that switching pages without re-applying leaves
	// both showing stale labeling for whatever's still actually painted on
	// the mesh.
	//
	// outTextColor is picked for legibility against the EXACT heatmap color
	// at this point (via SurfaceAnalysisOverlay::colorAt() + the same
	// lightness() < 128 -> white / else black convention this app's own
	// hatch-color picker already used) - a fixed white readout was
	// unreadable over the lighter/near-white bands of the color ramp
	// (confirmed real bug, not hypothetical: the Diverging colormap's own
	// white midpoint and the Sequential ramp's pale-yellow band are both
	// bright enough that white-on-white text vanished). Left untouched
	// (caller's own default) when this returns an empty string.
	QString hoverReadoutText(const MeshSurfaceAnchor& anchor, QColor& outTextColor) const;

protected:
	void closeEvent(QCloseEvent* event) override;

public slots:
	// Escape reaches here, not closeEvent() - same reasoning/precedent as
	// MeasurementDialog's own reject() override.
	void reject() override;

private slots:
	void onModeChanged();
	void onZebraStripeToggled(bool checked);
	void onApplyDraftAngleClicked();
	void onApplyDeviationClicked();
	void onApplyCurvatureClicked();
	void onApplyWallThicknessClicked();
	void onClearClicked();
	// The "highlight walls thinner than" checkbox / limit changed: re-colour the existing Wall-Thickness result
	// (threshold map or the continuous ramp) without recomputing anything.
	void onThicknessDisplayChanged();
	// Connected to MeshSelectionBox::meshUuidsChanged - the list of meshes this dialog acts on changed: refreshes
	// the Deviation reference choices and _selectionStatusLabel.
	void onSelectionChanged();
	// Connected to ViewportWidget::meshAboutToBeDeleted - both _overlay and
	// _zebraStripeMeshes hold raw SceneMesh* across event-loop turns (this
	// dialog is non-modal and can stay open across a delete), so they must
	// stop tracking a mesh before it's actually destroyed, not after. See
	// SurfaceAnalysisOverlay's own doc comment for why it can't protect
	// itself from this on its own.
	void onMeshAboutToBeDeleted(SceneMesh* mesh);
	void onImportUnitsChanged();
	// Idle staleness poll - isValid()'s first real caller (see
	// SurfaceAnalysisOverlay's own doc comment). Ticks on a QTimer, not a
	// push notification, since no transform-changed signal exists anywhere
	// on SceneMesh/RenderableMesh - reuses
	// RenderableMesh::currentRuntimeBoundsRevision() (already ticks on every
	// transform/geometry change) as a cheap "did anything worth re-checking
	// happen" gate before doing any real work.
	void checkForStaleOverlays();

	// Hides/shows this dialog as its own document's MDI subwindow loses/gains focus - mirrors RepairMeshDialog/
	// ShrinkWrapDialog/FillHolesDialog's identical mechanism (see the constructor's connect() for why).
	void onActiveSubWindowChanged(QMdiSubWindow* activeSubWindow);

private:
	enum class Mode { Curvature, WallThickness, Deviation };
	Mode currentMode() const;
	QVector3D currentPullDirection() const;

	// Applied to every mesh in the current selection - see this class's doc
	// comment on why the whole selection, not just one mesh: matches
	// MassPropertiesDialog's own "acts on the current selection" convention.
	void applyDraftAngleToSelection();
	void applyZebraStripeToSelection(bool active);
	void applyDeviationToSelection();
	void applyCurvatureToSelection();
	void applyWallThicknessToSelection();
	// Legend + one-line summary (thinnest wall, share of surface under the limit) for the meshes that currently
	// show a Wall-Thickness result.
	void updateThicknessLegendAndSummary();
	int thicknessDisplayBands() const;
	// Millimetres-per-unit scale for a mesh: multiplies its native-unit distances into millimetres, via the
	// same import-unit resolution Mass Properties uses.
	double lengthScaleForMesh(SceneMesh* mesh) const;
	// The "Clear Overlay" button: clears every overlay this dialog applied (see clearAllOverlays()) and hides the
	// legend/summary. Not scoped to the selection - see its definition for why.
	void clearSelectionOverlays();
	// Keeps the meshes being analyzed out of the Deviation reference picker - a mesh is not a valid reference for
	// itself. Called whenever the analyzed list changes.
	void syncReferenceExclusions();
	// The reference picker refused (or dropped) a mesh that is also being analyzed - tell the user why.
	void onReferenceMeshRejected();
	// Clears every overlay this dialog ever applied, regardless of what's
	// currently selected - used on close (see this class's doc comment),
	// since by then there's no button left to scope a selection-based clear.
	void clearAllOverlays();
	// Recomputes _selectionStatusLabel - a HINT only ("nothing selected", "Deviation needs exactly one"); the
	// meshes themselves are shown by _selectionBox, compactly. Depends on the list AND the active mode.
	void updateSelectionStatusLabel();
	// The mesh-store indices of the meshes this dialog acts on (the selection box's list) - what every Apply
	// handler reads instead of the viewer's live selection.
	std::vector<int> selectedMeshIds() const;

	// Disables every OTHER interactive control in the dialog and repurposes
	// `activeButton` into a Cancel button (label swapped to `buttonText`) -
	// same "disable everything except Cancel" idiom RtRenderDialog already
	// uses for offline ray-trace renders. Only one AnalysisComputeSession can
	// be in flight at a time dialog-wide (not just per-page) - this is what
	// makes that true, by preventing the user from switching pages/mode or
	// clicking a different Apply button while one is running. Called once
	// with (true, button, tr("Cancel")) before dispatch, once with (false,
	// button, <the button's own original text>) after.
	void setComputationInFlight(bool inFlight, QPushButton* activeButton, const QString& buttonText);

	// Non-null only while an AnalysisComputeSession's runBlocking() is on the
	// call stack somewhere below the current frame - NOT owned by this
	// dialog (it points at a stack-local object in whichever applyXToSelection()
	// is currently running); only ever dereferenced to call requestCancel(),
	// never to access anything that could outlive that stack frame. Lets a
	// re-click on the now-relabeled Cancel button, or closeEvent()/reject()
	// firing mid-computation (see their own doc comments on why the dialog
	// must not be allowed to actually close/be destroyed while a background
	// thread is still running), reach the active session.
	AnalysisComputeSession* _activeSession = nullptr;

	// Meshes deleted (via onMeshAboutToBeDeleted()) WHILE _activeSession was
	// non-null - a background computation's result can still come back
	// naming one of these via AnalysisComputeSession::PerMeshOutcome::meshHandle
	// (the worker itself never dereferences it, only carries it as an opaque
	// token - see AnalysisMeshSnapshot's own doc comment), but by the time
	// the caller gets it back the pointer is dangling. Checked and skipped
	// before any outcome's meshHandle is ever dereferenced; cleared at the
	// start of each applyXToSelection() call.
	QSet<SceneMesh*> _deletedWhileComputing;

	// Window geometry persistence - same QSettings("<key>/geometry") pattern
	// every other dialog in this app already uses (MeasurementDialog,
	// ShrinkWrapDialog, FillHolesDialog, etc.).
	void loadSettings();
	void saveSettings();

	ModelViewer* _modelViewer; // not owned - dialog is a transient child of the ModelViewer document

	MeshSelectionBox* _selectionBox = nullptr; // the meshes this dialog acts on
	QLabel* _selectionStatusLabel = nullptr;   // hint / notice line under it; hidden when empty

	QButtonGroup* _modeGroup = nullptr;
	QToolButton* _curvatureButton = nullptr;
	QToolButton* _thicknessButton = nullptr;
	QToolButton* _deviationButton = nullptr;
	QStackedWidget* _stack = nullptr;

	// Curvature page
	QPushButton* _zebraStripeToggle = nullptr; // checkable
	QPushButton* _applyCurvatureButton = nullptr;
	NotesListBox* _curvatureRepairNote = nullptr;

	// Wall-Thickness page
	QComboBox* _pullDirectionCombo = nullptr;
	QPushButton* _applyDraftButton = nullptr;
	QPushButton* _applyThicknessButton = nullptr;
	QComboBox* _thicknessMethodCombo = nullptr;       // Inscribed sphere / Local thickness / Normal ray (data = WallThicknessMethod)
	QCheckBox* _thicknessHighlightCheck = nullptr;    // switch the display to a pass/fail threshold map
	QDoubleSpinBox* _thicknessLimitSpin = nullptr;    // the limit, in mm
	QDoubleSpinBox* _thicknessSpreadSpin = nullptr;   // Local thickness ray spread (cone half angle), degrees
	QCheckBox* _thicknessEdgeReliefCheck = nullptr;   // Inscribed sphere: ignore the sharp-edge effect
	QComboBox* _thicknessDisplayCombo = nullptr;      // CATIA-style discrete ranges or smoothly interpolated ramp
	QSpinBox* _thicknessBandCountSpin = nullptr;
	QLabel* _thicknessSummaryLabel = nullptr;
	NotesListBox* _thicknessRejectionNote = nullptr;
	// Top of the continuous ramp (mm) for the current result: a robust percentile rather than the maximum, so one
	// long ray cannot squash everything else into a single colour.
	float _thicknessRangeMax = 1.0f;

	// What the hover log needs to explain a displayed thickness value: per mesh, the ray behind each
	// sub-triangle sample (see WallThicknessResult::sampleWitness), the grid that locates a sample from a surface
	// point, and the mesh-units -> mm factor. Replaced on every Wall-Thickness apply; only consulted while the
	// mesh's overlay is a wall-thickness one.
	struct ThicknessWitnessSet
	{
		std::vector<unsigned char> gridN;
		std::vector<unsigned int> offset;
		std::vector<WallThicknessWitness> witness;
		float toMm = 1.0f;
		bool sphere = false; // measured with the inscribed-sphere method (the witness is a contact, not a ray)
	};
	QHash<SceneMesh*, ThicknessWitnessSet> _thicknessWitness;
	// Last hovered sample that was logged, so a still cursor does not repeat the same line on every mouse move.
	mutable SceneMesh* _lastLoggedThicknessMesh = nullptr;
	mutable int _lastLoggedThicknessTriangle = -1;
	mutable int _lastLoggedThicknessSample = -1;
	// The witness of the sub-triangle sample under `anchor` (null if this mesh has none), with that sample's grid
	// resolution, index and the mesh-units -> mm factor.
	const WallThicknessWitness* thicknessWitnessAt(SceneMesh* mesh, const MeshSurfaceAnchor& anchor,
	                                               int& outGrid, int& outSample, float& outToMm) const;
	// Writes the ray behind the hovered sample to the log (once per sample). valueMm is NaN for a sample without
	// a value, which is then logged with the reason instead.
	void logThicknessWitness(SceneMesh* mesh, const MeshSurfaceAnchor& anchor, float valueMm) const;
	// Hover text for a sample that has no value - the reason, so a gap in the display explains itself. Empty if
	// the sample has a value or the mesh has no witness data.
	QString thicknessNoValueText(SceneMesh* mesh, const MeshSurfaceAnchor& anchor) const;
	static QString thicknessStatusText(WallThicknessSampleStatus status);

	// Deviation page
	MeshSelectionBox* _referenceBox = nullptr; // Deviation: the single reference mesh
	QPushButton* _applyDeviationButton = nullptr;

	QLabel* _legendLabel = nullptr;
	// Shared across all 3 pages (same reasoning as _legendLabel/_clearButton
	// above being shared rather than duplicated per page - the readout
	// itself is mode-agnostic, see hoverReadoutText()) - default checked.
	QPushButton* _hoverReadoutToggle = nullptr; // checkable
	QPushButton* _clearButton = nullptr;

	// Meshes zebra-stripe is currently active on - SurfaceAnalysisOverlay
	// only tracks the colormapped (Draft Angle) representation, so this
	// dialog tracks zebra-stripe state itself to know what to turn back off
	// in clearAllOverlays()/closeEvent().
	QSet<SceneMesh*> _zebraStripeMeshes;

	// Owns the lifecycle/cache-key bookkeeping for every mesh this dialog
	// has applied an overlay to - see that class's own doc comment. This
	// dialog is the "caller that owns a live instance" its doc comment
	// refers to.
	SurfaceAnalysisOverlay _overlay;

	// Drives checkForStaleOverlays() - see that slot's own doc comment.
	// Runs continuously once this dialog exists (cheap early-out inside the
	// slot itself when there's nothing tracked or nothing changed, rather
	// than starting/stopping the timer around visibility/overlay-count
	// transitions).
	QTimer* _stalenessTimer = nullptr;
	quint64 _lastSeenBoundsRevision = 0;
};
