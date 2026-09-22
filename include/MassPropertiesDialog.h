#pragma once

#include <QDialog>
#include <QPoint>
#include <QSet>
#include <QUuid>
#include <QVector>
#include <vector>

class QTableWidget;
class QLabel;
class QLineEdit;
class MeshSelectionBox;
class NotesListBox;
class QPushButton;
class QProgressBar;
class QCloseEvent;
class QMdiSubWindow;
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
// Non-modal, so meshes can be picked in the viewport while it is open. The set of meshes it reports on is its own
// list (seeded from the viewport selection when it opens), edited through the selection box at the top - the same
// pick / edit / clear affordances as the Exploded View panel's "Select assembly or meshes" box - and the report is
// recomputed whenever that list changes (or on Recalculate): a snapshot of the moment, not a live-tracking panel.
// Right-clicking a row of the table offers Center Screen / Hide / Show for the meshes on the selected rows.
// ---------------------------------------------------------------------------
class MassPropertiesDialog : public QDialog
{
	Q_OBJECT
public:
	explicit MassPropertiesDialog(ModelViewer* modelViewer, QWidget* parent = nullptr);

	// Re-seeds the mesh list from the viewport's current selection (used when the tool is invoked again while this
	// dialog is already open) and recomputes. Does nothing if nothing is selected in the viewport.
	void seedFromViewportSelection();

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

	// The selection box's list changed - recompute the report for it.
	void onSelectionListChanged();
	// Right-click on a row of the results table.
	void showTableContextMenu(const QPoint& pos);
	// Selecting rows selects the same meshes in the viewer (and scene tree). One way only: selecting in the viewer
	// does not change the rows. Ignored while the pick button is on (the viewer selection is then being gathered)
	// and while the table is being rebuilt.
	void onTableRowSelectionChanged();

	// Hides/shows this dialog as its own document's MDI subwindow loses/gains focus - mirrors RepairMeshDialog/
	// ShrinkWrapDialog/FillHolesDialog's identical mechanism (see the constructor's connect() for why).
	void onActiveSubWindowChanged(QMdiSubWindow* activeSubWindow);

private:
	void populate();

	// The meshes behind the currently selected table rows (the right-clicked row if it was not selected).
	QVector<QUuid> meshesOfSelectedRows() const;

	// Hides every _table row whose Mesh/Material column doesn't contain _searchEdit's text (case-insensitive) -
	// purely visual, does not touch the underlying data or trigger a recompute. Re-run after populate() refills the
	// table and after every sortItems() call (row hidden-state is tracked by physical row index, which sorting
	// reassigns - recomputing from scratch, rather than trying to carry the old state along, is what stays correct
	// regardless of how sortItems() happens to move things internally).
	void applyTableSearchFilter();

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

	MeshSelectionBox* _selectionBox = nullptr; // the meshes this report covers (its list order = table row order)
	QVector<QUuid> _rowUuids;        // the mesh behind each table row of the last populate() (row order)
	bool _suppressRowSync = false;   // true while populate() rebuilds the table - see onTableRowSelectionChanged()
	int _sortColumn = -1;            // the column the table is sorted by (-1 = the selection's own order)
	Qt::SortOrder _sortOrder = Qt::AscendingOrder;
	int _materialSortColumn = -1;    // the same for the Mass by Material table
	Qt::SortOrder _materialSortOrder = Qt::AscendingOrder;
	QPushButton* _recalculateButton = nullptr;

	NotesListBox* _notesBox = nullptr; // units / density / shell footnotes, refreshed per populate() - height-capped, so it never pushes the dialog off-screen
	QLabel* _noSelectionLabel = nullptr;
	QLineEdit* _searchEdit = nullptr; // filters _table's rows by Mesh/Material text - see applyTableSearchFilter()
	QTableWidget* _table = nullptr;
	QLabel* _totalsLabel = nullptr;
	QLabel* _materialBreakdownLabel = nullptr;
	QTableWidget* _materialTable = nullptr; // per-material mass breakdown - own scrollable table (P2 Codex fix: an assembly with many materials must not force the dialog past the screen)
	QProgressBar* _progressBar = nullptr; // visible only while a computation is in flight
	QPushButton* _closeButton = nullptr;
};
