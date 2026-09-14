#pragma once

#include <QDialog>
#include <QSet>

class QTableWidget;
class QLabel;
class QPushButton;
class QProgressBar;
class QCloseEvent;
class ModelViewer;
class SceneMesh;
class AnalysisComputeSession;

// ---------------------------------------------------------------------------
// MassPropertiesDialog (Tools -> Mass Properties...)
//
// Per-mesh + assembly-total report for the current selection: volume,
// surface area, mass, geometric centroid, and mass-weighted center of mass -
// built on the corrected surface-area/volume/centroid math in
// MeshProperties.h's free functions (computeMeshGeometry() etc. - see their
// own doc comments for the centroid-sign/stale-cache/fake-density bugs this
// replaces; a QObject-derived MeshProperties class originally held this
// logic, replaced once this dialog moved to a background
// AnalysisComputeSession, which needs a plain point/index snapshot rather
// than a live SceneMesh*). Split out of the old tree-context-menu "Mesh
// Info" dump (ModelViewer::displaySelectedMeshInfo(), now trimmed to just
// points/triangles/memory) specifically because THIS data needs real
// widgets, not a single QMessageBox string: an open mesh has no valid
// volume, a mesh whose material has no assigned density has no valid mass,
// and a mixed selection needs a known-value subtotal + excluded-mesh count
// rather than a single number that silently ignores the excluded ones.
//
// Pure C++ widget construction (no .ui file), matching BatchRenderViewsDialog's
// convention from this same session (a hand-written .ui XML file can't be
// visually verified without Designer, while plain C++ construction is
// exactly the same code either way and stays fully inspectable).
//
// Modal, one-shot report - recomputed fresh every time it's opened (same
// "snapshot, not a live-tracking panel" shape as the old context-menu
// dialog it replaces), not a persistent dockable tool.
// ---------------------------------------------------------------------------
class MassPropertiesDialog : public QDialog
{
	Q_OBJECT
public:
	explicit MassPropertiesDialog(ModelViewer* modelViewer, QWidget* parent = nullptr);

protected:
	void closeEvent(QCloseEvent* event) override;

public slots:
	// Escape reaches here, not closeEvent() (QDialog::reject() only hide()s -
	// same double-override pattern SurfaceAnalysisDialog/MeasurementDialog
	// already use to make sure geometry still gets saved on Escape too).
	void reject() override;

private slots:
	// _closeButton's real click handler - while a computation is in flight
	// this cancels it instead of closing (see setComputationInFlight()'s own
	// doc comment); connect(_closeButton, ..., &QWidget::close) directly, the
	// original wiring, could never distinguish the two.
	void onCloseButtonClicked();
	// Connected to ViewportWidget::meshAboutToBeDeleted - populate()'s
	// AnalysisComputeSession::runBlocking() call runs its own nested event
	// loop, which (even though this dialog isn't shown yet during its own
	// constructor - see populate()'s own doc comment) still lets the REST of
	// the app process events, including a scene-tree delete of a mesh this
	// computation is currently processing. See _deletedWhileComputing's own
	// doc comment.
	void onMeshAboutToBeDeleted(SceneMesh* mesh);

private:
	void populate();

	// Window geometry persistence - same QSettings("<key>/geometry") pattern
	// every other dialog in this app already uses.
	void loadSettings();
	void saveSettings();

	// Same "disable everything except Cancel, repurpose the one relevant
	// button" idiom SurfaceAnalysisDialog/RtRenderDialog already use -
	// MassPropertiesDialog has no mode selector/Apply button of its own to
	// disable (populate() runs automatically, not from a manual trigger), so
	// this only ever repurposes _closeButton itself.
	void setComputationInFlight(bool inFlight);

	AnalysisComputeSession* _activeSession = nullptr; // non-null only while populate()'s runBlocking() call is in flight - see its own doc comment

	// Meshes deleted (via onMeshAboutToBeDeleted()) WHILE _activeSession was
	// non-null - a background result can still come back naming one of
	// these via AnalysisComputeSession::PerMeshOutcome::meshHandle (the
	// worker itself never dereferences it, only carries it as an opaque
	// token), but by the time populate() gets it back the pointer is
	// dangling. Checked and skipped before any outcome's meshHandle is ever
	// dereferenced; cleared at the start of populate().
	QSet<SceneMesh*> _deletedWhileComputing;

	ModelViewer* _modelViewer; // not owned - dialog is a transient child of the ModelViewer document

	QLabel* _unitsNoteLabel = nullptr; // text refreshed per populate() - see its own doc comment
	QLabel* _noSelectionLabel = nullptr;
	QTableWidget* _table = nullptr;
	QLabel* _totalsLabel = nullptr;
	QLabel* _materialBreakdownLabel = nullptr;
	QTableWidget* _materialTable = nullptr; // per-material mass breakdown - own scrollable table (P2 Codex fix: an assembly with many materials must not force the dialog past the screen)
	QProgressBar* _progressBar = nullptr; // visible only while a computation is in flight
	QPushButton* _closeButton = nullptr;
};
