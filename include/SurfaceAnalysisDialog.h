#pragma once

#include <QDialog>
#include <QVector3D>
#include <QSet>

#include "SurfaceAnalysisOverlay.h"

class QToolButton;
class QButtonGroup;
class QStackedWidget;
class QLabel;
class QComboBox;
class QPushButton;
class QCloseEvent;
class ModelViewer;
class SceneMesh;

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
    void selectMode(const QString& mode);

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
	// Connected to ViewportWidget::selectionChanged - keeps _selectionStatusLabel
	// live as the user selects/deselects in the scene tree while this
	// non-modal dialog stays open, rather than only ever surfacing "nothing
	// selected" as an error after the fact when Apply is clicked.
	void onSelectionChanged();
	// Connected to ViewportWidget::meshAboutToBeDeleted - both _overlay and
	// _zebraStripeMeshes hold raw SceneMesh* across event-loop turns (this
	// dialog is non-modal and can stay open across a delete), so they must
	// stop tracking a mesh before it's actually destroyed, not after. See
	// SurfaceAnalysisOverlay's own doc comment for why it can't protect
	// itself from this on its own.
	void onMeshAboutToBeDeleted(SceneMesh* mesh);

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
	void clearSelectionOverlays();
	// Repopulates _referenceMeshCombo from the document's currently loaded
	// meshes, excluding the current selection - called whenever the
	// Deviation page becomes active, since the loaded-mesh list can change
	// between visits (import, delete) while the dialog stays open.
	void refreshReferenceMeshCombo();
	// Clears every overlay this dialog ever applied, regardless of what's
	// currently selected - used on close (see this class's doc comment),
	// since by then there's no button left to scope a selection-based clear.
	void clearAllOverlays();
	// Recomputes _selectionStatusLabel's text from the viewport's current
	// selection AND the currently active mode - Deviation's "exactly one
	// mesh" requirement reads differently from the other modes' "whole
	// selection" convention, so the wording depends on both.
	void updateSelectionStatusLabel();

	// Window geometry persistence - same QSettings("<key>/geometry") pattern
	// every other dialog in this app already uses (MeasurementDialog,
	// ShrinkWrapDialog, FillHolesDialog, etc.).
	void loadSettings();
	void saveSettings();

	ModelViewer* _modelViewer; // not owned - dialog is a transient child of the ModelViewer document

	QLabel* _selectionStatusLabel = nullptr;

	QButtonGroup* _modeGroup = nullptr;
	QToolButton* _curvatureButton = nullptr;
	QToolButton* _thicknessButton = nullptr;
	QToolButton* _deviationButton = nullptr;
	QStackedWidget* _stack = nullptr;

	// Curvature page
	QPushButton* _zebraStripeToggle = nullptr; // checkable
	QPushButton* _applyCurvatureButton = nullptr;
	QLabel* _curvatureRepairNote = nullptr;

	// Wall-Thickness page
	QComboBox* _pullDirectionCombo = nullptr;
	QPushButton* _applyDraftButton = nullptr;
	QPushButton* _applyThicknessButton = nullptr;
	QLabel* _thicknessRejectionNote = nullptr;

	// Deviation page
	QComboBox* _referenceMeshCombo = nullptr;
	QPushButton* _applyDeviationButton = nullptr;

	QLabel* _legendLabel = nullptr;
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
};
