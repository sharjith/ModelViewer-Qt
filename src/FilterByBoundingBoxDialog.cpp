#include "FilterByBoundingBoxDialog.h"
#include "ModelViewer.h"
#include "ViewportWidget.h"
#include "SceneMesh.h"
#include "PlaneGizmoDragCommand.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QPointer>
#include <QRadioButton>
#include <QShowEvent>
#include <QHideEvent>
#include <QCloseEvent>
#include <QEvent>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QSet>
#include <QUndoStack>
#include <QSignalBlocker>
#include <QSettings>
#include <QFont>

namespace
{
	// Wide enough for real CAD coordinates (import units range from mm to m,
	// and a part can legitimately sit far from the origin) without the field
	// clamping a typed value - QDoubleSpinBox's own default range (0-99.99)
	// would silently reject almost every real-world value otherwise.
	constexpr double kSpinRange = 1.0e7;
	constexpr int kSpinDecimals = 3;
	// Minimum gap a drag must leave between a Min/Max face pair - prevents
	// dragging a face past its partner into a degenerate/inverted box.
	// kMinBoxGap now lives in BoundingBox.h (shared with the Clipping Planes editor's box mode).

	// Walks up the parent chain from a widget inside the MDI area to find the
	// QMdiArea itself - same helper as FilterByColorDialog.cpp/
	// FilterByMaterialDialog.cpp, redeclared locally per those files' own
	// convention.
	QMdiArea* findMdiArea(QWidget* widget)
	{
		for (QWidget* w = widget; w; w = w->parentWidget())
		{
			if (auto* area = qobject_cast<QMdiArea*>(w))
				return area;
		}
		return nullptr;
	}

	QDoubleSpinBox* makeLimitSpin(QWidget* parent, double initialValue)
	{
		auto* spin = new QDoubleSpinBox(parent);
		spin->setRange(-kSpinRange, kSpinRange);
		spin->setDecimals(kSpinDecimals);
		spin->setValue(initialValue);
		return spin;
	}
}

FilterByBoundingBoxDialog::FilterByBoundingBoxDialog(ModelViewer* modelViewer,
                                                      const BoundingBox& initialBounds,
                                                      QWidget* parent)
	: QDialog(parent)
	, _modelViewer(modelViewer)
{
	setWindowTitle(tr("Filter by Bounding Box"));
	resize(400, 360);
	setAttribute(Qt::WA_DeleteOnClose);

	const bool zUp = _modelViewer && _modelViewer->getViewportWidget()
		&& _modelViewer->getViewportWidget()->isCameraUpAxisZUp();

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(16, 16, 16, 16);
	layout->setSpacing(12);

	auto* introLabel = new QLabel(tr("Set world-space X/Y/Z limits to preview every mesh in the "
	                                  "scene whose bounding box matches, then Show Only or Hide "
	                                  "the result:"), this);
	introLabel->setWordWrap(true);
	layout->addWidget(introLabel);

	auto* limitsGroup = new QGroupBox(tr("Limits"), this);
	auto* grid = new QGridLayout(limitsGroup);
	grid->addWidget(new QLabel(tr("Min"), limitsGroup), 0, 1, Qt::AlignCenter);
	grid->addWidget(new QLabel(tr("Max"), limitsGroup), 0, 2, Qt::AlignCenter);

	auto* xLabel = new QLabel(tr("X"), limitsGroup);
	_xMinSpin = makeLimitSpin(limitsGroup, initialBounds.xMin());
	_xMaxSpin = makeLimitSpin(limitsGroup, initialBounds.xMax());
	grid->addWidget(xLabel, 1, 0);
	grid->addWidget(_xMinSpin, 1, 1);
	grid->addWidget(_xMaxSpin, 1, 2);

	// "(Height)" suffix follows the app's current up-axis convention (read
	// once here - see this class's own doc comment for why not live) -
	// rows stay in fixed X/Y/Z order regardless, only the label text changes.
	_yRowLabel = new QLabel(zUp ? tr("Y") : tr("Y (Height)"), limitsGroup);
	_yMinSpin = makeLimitSpin(limitsGroup, initialBounds.yMin());
	_yMaxSpin = makeLimitSpin(limitsGroup, initialBounds.yMax());
	grid->addWidget(_yRowLabel, 2, 0);
	grid->addWidget(_yMinSpin, 2, 1);
	grid->addWidget(_yMaxSpin, 2, 2);

	_zRowLabel = new QLabel(zUp ? tr("Z (Height)") : tr("Z"), limitsGroup);
	_zMinSpin = makeLimitSpin(limitsGroup, initialBounds.zMin());
	_zMaxSpin = makeLimitSpin(limitsGroup, initialBounds.zMax());
	grid->addWidget(_zRowLabel, 3, 0);
	grid->addWidget(_zMinSpin, 3, 1);
	grid->addWidget(_zMaxSpin, 3, 2);

	grid->setColumnStretch(1, 1);
	grid->setColumnStretch(2, 1);
	layout->addWidget(limitsGroup);

	_useSelectionBoundsButton = new QPushButton(tr("Use Current Selection's Bounds"), this);
	_useSelectionBoundsButton->setToolTip(tr("Fill the six limits above with the combined bounding "
	                                          "box of whatever is currently selected in the viewport."));
	layout->addWidget(_useSelectionBoundsButton);

	auto* containmentGroup = new QGroupBox(tr("Match When"), this);
	auto* containmentLayout = new QHBoxLayout(containmentGroup);
	_anyOverlapRadio = new QRadioButton(tr("Any Overlap"), containmentGroup);
	_anyOverlapRadio->setToolTip(tr("Select every mesh whose bounding box overlaps the limits at all."));
	_fullyInsideRadio = new QRadioButton(tr("Fully Inside"), containmentGroup);
	_fullyInsideRadio->setToolTip(tr("Select only meshes whose bounding box is fully enclosed by the limits."));
	_anyOverlapRadio->setChecked(true);
	containmentLayout->addWidget(_anyOverlapRadio);
	containmentLayout->addWidget(_fullyInsideRadio);
	layout->addWidget(containmentGroup);

	_matchCountLabel = new QLabel(this);
	_matchCountLabel->setAlignment(Qt::AlignCenter);
	_matchCountLabel->setWordWrap(true);
	layout->addWidget(_matchCountLabel);

	auto* buttonRow = new QHBoxLayout();
	buttonRow->addStretch();
	_showOnlyButton = new QPushButton(tr("Show Only"), this);
	_hideButton = new QPushButton(tr("Hide"), this);
	buttonRow->addWidget(_showOnlyButton);
	buttonRow->addWidget(_hideButton);
	layout->addLayout(buttonRow);

	for (QDoubleSpinBox* spin : { _xMinSpin, _xMaxSpin, _yMinSpin, _yMaxSpin, _zMinSpin, _zMaxSpin })
		connect(spin, &QDoubleSpinBox::valueChanged, this, &FilterByBoundingBoxDialog::onLimitsChanged);
	connect(_anyOverlapRadio, &QRadioButton::toggled, this, &FilterByBoundingBoxDialog::onContainmentModeChanged);
	connect(_useSelectionBoundsButton, &QPushButton::clicked, this, &FilterByBoundingBoxDialog::onUseSelectionBoundsClicked);
	connect(_showOnlyButton, &QPushButton::clicked, this, &FilterByBoundingBoxDialog::onShowOnlyClicked);
	connect(_hideButton, &QPushButton::clicked, this, &FilterByBoundingBoxDialog::onHideClicked);

	// Hide/show this dialog as its OWN document's MDI subwindow loses/gains
	// focus - mirrors FilterByColorDialog's identical mechanism.
	if (_modelViewer)
	{
		if (QMdiArea* mdiArea = findMdiArea(_modelViewer))
			connect(mdiArea, &QMdiArea::subWindowActivated, this, &FilterByBoundingBoxDialog::onActiveSubWindowChanged);

		// Rebuilds on ANY undo/redo/push on this document - a mesh's world
		// bounding box can change from things this dialog has no other way
		// to observe (undoing/redoing a transform, Group/Ungroup, ...).
		// Goes through onUndoStackIndexChanged() rather than updateMatches()
		// directly for the same two reasons documented on
		// FilterByColorDialog's identical connection: Qt::QueuedConnection
		// avoids re-entering QUndoStack::push() while undo()/redo() is still
		// on the call stack, and the suppress-flag stops this dialog from
		// silently reversing the user's own undo/redo by re-pushing the same
		// match set right back.
		if (QUndoStack* undoStack = _modelViewer->getUndoStack())
			connect(undoStack, &QUndoStack::indexChanged, this, &FilterByBoundingBoxDialog::onUndoStackIndexChanged, Qt::QueuedConnection);
	}

	// Draggable 6-face box gizmo in the viewport - the primary way to adjust
	// the limits, with these spin boxes as a precise refinement on top (per
	// explicit direction: the gizmo is the primary manipulation mode here).
	// createBoundingBoxGizmos() is idempotent, so this is safe even if a
	// PREVIOUS FilterByBoundingBoxDialog instance for this same document
	// already created them - re-wiring onDragged below every time is still
	// required, though: that previous instance is gone (WA_DeleteOnClose),
	// so its own lambdas captured a now-dangling `this` and MUST be
	// overwritten with fresh ones bound to this instance.
	if (_modelViewer && _modelViewer->getViewportWidget())
	{
		ViewportWidget* vw = _modelViewer->getViewportWidget();
		vw->createBoundingBoxGizmos();
		// Each face writes into its own one paired spin box, clamped so it
		// can never cross its partner (Min can't pass Max and vice versa) -
		// setValue() fires valueChanged -> onLimitsChanged() -> updateMatches(),
		// which also repositions all 6 gizmos (including the other 4 whose
		// SIZE - not position - depends on this one value), so no separate
		// reposition call is needed here.
		vw->bboxGizmoXMin()->onDragged = [this](float worldX) {
			_xMinSpin->setValue(std::min(static_cast<double>(worldX), _xMaxSpin->value() - kMinBoxGap));
		};
		vw->bboxGizmoXMax()->onDragged = [this](float worldX) {
			_xMaxSpin->setValue(std::max(static_cast<double>(worldX), _xMinSpin->value() + kMinBoxGap));
		};
		vw->bboxGizmoYMin()->onDragged = [this](float worldY) {
			_yMinSpin->setValue(std::min(static_cast<double>(worldY), _yMaxSpin->value() - kMinBoxGap));
		};
		vw->bboxGizmoYMax()->onDragged = [this](float worldY) {
			_yMaxSpin->setValue(std::max(static_cast<double>(worldY), _yMinSpin->value() + kMinBoxGap));
		};
		vw->bboxGizmoZMin()->onDragged = [this](float worldZ) {
			_zMinSpin->setValue(std::min(static_cast<double>(worldZ), _zMaxSpin->value() - kMinBoxGap));
		};
		vw->bboxGizmoZMax()->onDragged = [this](float worldZ) {
			_zMaxSpin->setValue(std::max(static_cast<double>(worldZ), _zMinSpin->value() + kMinBoxGap));
		};

		// One undo step per completed drag (not per mouse-move frame, and
		// not for direct spin-box typing) - see PlaneGizmoDragCommand's own
		// doc comment. The setter is just spin->setValue(), the exact same
		// call onDragged above already makes, so undo/redo goes through the
		// identical valueChanged -> onLimitsChanged -> updateMatches path a
		// live drag does.
		auto wireDragUndo = [this, vw](PlaneGizmo* gizmo, QDoubleSpinBox* spin, const QString& text) {
			auto oldValue = std::make_shared<double>(0.0);
			auto oldSelection = std::make_shared<QSet<int>>();
			gizmo->onDragStarted = [this, oldValue, oldSelection, spin]() {
				*oldValue = spin->value();
				const std::vector<int> ids = _modelViewer->getSelectedIDs();
				*oldSelection = QSet<int>(ids.cbegin(), ids.cend());
				// Live-drag frames (onDragged -> onLimitsChanged ->
				// updateMatches()) apply the tracking selection directly
				// instead of pushing one SelectionCommand per frame - see
				// _dragLiveSelectionSyncActive's doc comment. Cleared in
				// onDragFinished below, once the final selection has been
				// captured.
				_dragLiveSelectionSyncActive = true;
			};
			gizmo->onDragFinished = [this, vw, oldValue, oldSelection, spin, text]() {
				_dragLiveSelectionSyncActive = false;
				const double newValue = spin->value();
				if (std::abs(newValue - *oldValue) < kMinBoxGap)
					return; // click with no real movement - nothing to undo
				// The selection already tracks the finished drag's final box
				// (applied live, frame by frame, while _dragLiveSelectionSyncActive
				// was set) - capture it now rather than recomputing, so the
				// undo command below can restore it exactly on redo. Snapshot
				// both into plain QSet<int> values (not the reused oldSelection
				// shared_ptr) - wireDragUndo's onDragStarted overwrites that
				// shared_ptr's pointee on the NEXT drag of this same face, which
				// would otherwise silently corrupt THIS command's already-
				// captured old selection out from under it.
				const std::vector<int> currentIds = _modelViewer->getSelectedIDs();
				const QSet<int> newSelectionSnapshot(currentIds.cbegin(), currentIds.cend());
				const QSet<int> oldSelectionSnapshot = *oldSelection;
				// spin is a child of THIS dialog (WA_DeleteOnClose), but the
				// command below lives on the document's own QUndoStack, which
				// outlives the dialog - closing it and later hitting Undo/Redo
				// would otherwise call setValue() on an already-destroyed
				// QDoubleSpinBox. QPointer-guard the setter so it safely no-ops
				// once the dialog is gone instead of a use-after-free; reopening
				// this filter already starts from a fresh seed (see the
				// constructor's own doc comment), so there is no durable
				// per-document state left for the entry to usefully apply to
				// at that point anyway.
				QPointer<QDoubleSpinBox> spinPtr(spin);
				const float capturedOldValue = static_cast<float>(*oldValue);
				// Captured by value (not read through `this->_modelViewer`
				// inside the setter below) - _modelViewer is a member of THIS
				// dialog, so touching it through `this` after the dialog is
				// closed (WA_DeleteOnClose) would itself be a use-after-free,
				// independent of spinPtr's own null check. The command already
				// lives on _modelViewer's own QUndoStack, which by construction
				// outlives both the dialog and the command, so this raw pointer
				// needs no separate guard.
				ModelViewer* const modelViewerPtr = _modelViewer;
				_modelViewer->getUndoStack()->push(new PlaneGizmoDragCommand(
					_modelViewer, vw,
					[this, spinPtr, modelViewerPtr, oldSelectionSnapshot, newSelectionSnapshot, capturedOldValue](float v) {
						// spinPtr null means the dialog (and `this`) is gone -
						// skip only the now-defunct spin-box/live-filter update
						// in that case, never touch `this` itself. Confirmed
						// real bug: returning here unconditionally used to also
						// skip the selection restoration below, so Undo/Redo
						// consumed this command's history entry without ever
						// reversing its selection change once the dialog had
						// been closed.
						if (spinPtr)
						{
							// spinPtr->setValue() below synchronously re-enters
							// onLimitsChanged() -> updateMatches() via the DIRECT
							// (non-queued) valueChanged connection - unlike the
							// indexChanged-driven rebuild (onUndoStackIndexChanged(),
							// deliberately Qt::QueuedConnection precisely to avoid
							// this), this path has no existing guard against
							// updateMatches() pushing a brand new SelectionCommand
							// while THIS command's own undo()/redo() is still on
							// the QUndoStack call stack. Same suppression flag,
							// same reasoning, just applied at the other trigger
							// site.
							_suppressLiveSelectionPush = true;
							spinPtr->setValue(v);
							_suppressLiveSelectionPush = false;
						}
						// v is handed back verbatim from PlaneGizmoDragCommand -
						// bit-identical to capturedOldValue on undo() or to the
						// pushed newValue on redo() - so comparing against it
						// reliably picks the matching captured selection,
						// restoring limit and selection together as ONE undo
						// step instead of the two separate entries a plain
						// live-drag SelectionCommand + plane command would
						// otherwise leave behind. Runs unconditionally (even
						// with the dialog closed) - it's durable document
						// selection state, not tied to the dialog's own
						// lifetime the way the spin box is.
						modelViewerPtr->setSelectionWithoutUndo(v == capturedOldValue ? oldSelectionSnapshot : newSelectionSnapshot);
					},
					capturedOldValue, static_cast<float>(newValue), text));
			};
		};
		wireDragUndo(vw->bboxGizmoXMin(), _xMinSpin, tr("Drag Bounding Box Face"));
		wireDragUndo(vw->bboxGizmoXMax(), _xMaxSpin, tr("Drag Bounding Box Face"));
		wireDragUndo(vw->bboxGizmoYMin(), _yMinSpin, tr("Drag Bounding Box Face"));
		wireDragUndo(vw->bboxGizmoYMax(), _yMaxSpin, tr("Drag Bounding Box Face"));
		wireDragUndo(vw->bboxGizmoZMin(), _zMinSpin, tr("Drag Bounding Box Face"));
		wireDragUndo(vw->bboxGizmoZMax(), _zMaxSpin, tr("Drag Bounding Box Face"));
	}

	loadSettings();
	updateMatches();
}

void FilterByBoundingBoxDialog::showEvent(QShowEvent* event)
{
	QDialog::showEvent(event);
	if (_modelViewer && _modelViewer->getViewportWidget())
		_modelViewer->getViewportWidget()->setBoundingBoxGizmosVisible(true);
	// The scene may have changed while this dialog was hidden behind another
	// document tab - recompute against a fresh mesh store rather than
	// trusting a stale match set.
	updateMatches();
}

void FilterByBoundingBoxDialog::hideEvent(QHideEvent* event)
{
	QDialog::hideEvent(event);
	if (_modelViewer && _modelViewer->getViewportWidget())
		_modelViewer->getViewportWidget()->setBoundingBoxGizmosVisible(false);
}

void FilterByBoundingBoxDialog::onActiveSubWindowChanged(QMdiSubWindow* activeSubWindow)
{
	const bool isOwnDocumentActive = _modelViewer
		&& activeSubWindow
		&& activeSubWindow->widget() == static_cast<QWidget*>(_modelViewer);
	setVisible(isOwnDocumentActive);
}

void FilterByBoundingBoxDialog::changeEvent(QEvent* event)
{
	QDialog::changeEvent(event);
	if (event->type() == QEvent::ActivationChange && isActiveWindow())
		updateMatches();
}

void FilterByBoundingBoxDialog::closeEvent(QCloseEvent* event)
{
	saveSettings();
	QDialog::closeEvent(event);
}

void FilterByBoundingBoxDialog::reject()
{
	saveSettings();
	QDialog::reject();
}

void FilterByBoundingBoxDialog::loadSettings()
{
	QSettings settings;
	const QByteArray geometry = settings.value("filterByBoundingBox/geometry", QByteArray()).toByteArray();
	if (!geometry.isEmpty())
		restoreGeometry(geometry);
}

void FilterByBoundingBoxDialog::saveSettings()
{
	QSettings settings;
	settings.setValue("filterByBoundingBox/geometry", saveGeometry());
}

void FilterByBoundingBoxDialog::onLimitsChanged()
{
	updateMatches();
}

void FilterByBoundingBoxDialog::onContainmentModeChanged()
{
	updateMatches();
}

void FilterByBoundingBoxDialog::onUseSelectionBoundsClicked()
{
	if (!_modelViewer || !_modelViewer->getViewportWidget())
		return;

	const std::vector<int> selectedIds = _modelViewer->getSelectedIDs();
	if (selectedIds.empty())
		return;

	const std::vector<SceneMesh*> meshStore = _modelViewer->getViewportWidget()->getMeshStore();
	BoundingBox combined;
	bool any = false;
	for (int id : selectedIds)
	{
		if (id < 0 || id >= static_cast<int>(meshStore.size()) || !meshStore[id])
			continue;
		if (!any)
		{
			combined = meshStore[id]->getBoundingBox();
			any = true;
		}
		else
		{
			combined.addBox(meshStore[id]->getBoundingBox());
		}
	}
	if (!any)
		return;

	const QSignalBlocker xMinBlock(_xMinSpin), xMaxBlock(_xMaxSpin);
	const QSignalBlocker yMinBlock(_yMinSpin), yMaxBlock(_yMaxSpin);
	const QSignalBlocker zMinBlock(_zMinSpin), zMaxBlock(_zMaxSpin);
	_xMinSpin->setValue(combined.xMin());
	_xMaxSpin->setValue(combined.xMax());
	_yMinSpin->setValue(combined.yMin());
	_yMaxSpin->setValue(combined.yMax());
	_zMinSpin->setValue(combined.zMin());
	_zMaxSpin->setValue(combined.zMax());

	updateMatches();
}

void FilterByBoundingBoxDialog::updateMatches()
{
	if (!_modelViewer || !_modelViewer->getViewportWidget())
		return;

	// BoundingBox takes limits in axis pairs: X min/max, Y min/max, Z min/max.
	const BoundingBox limits(_xMinSpin->value(), _xMaxSpin->value(),
	                          _yMinSpin->value(), _yMaxSpin->value(),
	                          _zMinSpin->value(), _zMaxSpin->value());
	const bool fullyInside = _fullyInsideRadio->isChecked();

	// Keeps the 6-face gizmo in sync with every path that can change a
	// limit - direct spin-box edits (valueChanged -> onLimitsChanged() ->
	// here), "Use Current Selection's Bounds", and this dialog's own initial
	// construction - one call site instead of repeating it at each of them.
	_modelViewer->getViewportWidget()->updateBoundingBoxGizmos(limits);

	std::vector<SceneMesh*> meshStore = _modelViewer->getViewportWidget()->getMeshStore();

	std::vector<int> matchingIndices;
	for (int i = 0; i < static_cast<int>(meshStore.size()); ++i)
	{
		SceneMesh* mesh = meshStore[i];
		if (!mesh)
			continue;
		const BoundingBox meshBox = mesh->getBoundingBox();
		const bool matches = fullyInside ? limits.contains(meshBox) : limits.intersects(meshBox);
		if (matches)
			matchingIndices.push_back(i);
	}

	const bool hasMatches = !matchingIndices.empty();
	_matchCountLabel->setText(hasMatches
		? tr("%1 mesh(es) match.").arg(matchingIndices.size())
		: tr("No meshes match the current limits."));

	// Theme-safe emphasis via palette color group rather than a hardcoded
	// hex - same idiom as FilterByColorDialog::updateMatches().
	QPalette pal = _matchCountLabel->palette();
	pal.setColor(QPalette::WindowText, pal.color(hasMatches ? QPalette::Active : QPalette::Disabled, QPalette::WindowText));
	_matchCountLabel->setPalette(pal);
	QFont font = _matchCountLabel->font();
	font.setBold(hasMatches);
	_matchCountLabel->setFont(font);

	_showOnlyButton->setEnabled(hasMatches);
	_hideButton->setEnabled(hasMatches);

	const QSet<int> newSelection(matchingIndices.cbegin(), matchingIndices.cend());
	const std::vector<int> currentIds = _modelViewer->getSelectedIDs();
	const QSet<int> currentSelection(currentIds.cbegin(), currentIds.cend());
	if (newSelection == currentSelection)
		return;

	// Live plane-gizmo drag in progress: keep the highlighted selection
	// tracking the box in real time, but apply it directly (bypassing the
	// undo stack) instead of pushing a SelectionCommand per frame - the
	// whole drag folds into the single PlaneGizmoDragCommand pushed at
	// onDragFinished (see _dragLiveSelectionSyncActive's doc comment).
	if (_dragLiveSelectionSyncActive)
	{
		_modelViewer->setSelectionWithoutUndo(newSelection);
		return;
	}

	// Never touch the selection from an undo/redo-triggered rebuild - see
	// _suppressLiveSelectionPush's doc comment. Confirmed real bug: reapplying
	// the recomputed selection here (even via the non-undo setter) fought a
	// plain Undo of one of this dialog's own SelectionCommands, since that
	// command restores the selection alone without touching the spin boxes -
	// the very next queued rebuild would recompute the SAME selection that
	// was just undone and silently reinstate it.
	if (_suppressLiveSelectionPush)
		return;

	// mergeSource == this: consecutive limit/mode tweaks while this dialog
	// stays open collapse into one undo step (see SelectionCommand's
	// mergeWith()) - same mechanism FilterByColorDialog::updateMatches() uses.
	_modelViewer->setSelectionWithUndo(newSelection, this);
}

void FilterByBoundingBoxDialog::onUndoStackIndexChanged()
{
	// Same reasoning as FilterByColorDialog::onUndoStackIndexChanged(): this
	// dialog unconditionally re-asserts its selection to match the current
	// limits every update (it's a live filter) - including right after
	// undoing/redoing a selection THIS dialog itself previously pushed,
	// which would recompute that SAME match set and push it right back,
	// silently reversing the user's own undo/redo. Suppressing the push for
	// the duration breaks that loop; the match-count display still refreshes.
	_suppressLiveSelectionPush = true;
	updateMatches();
	_suppressLiveSelectionPush = false;
}

void FilterByBoundingBoxDialog::onShowOnlyClicked()
{
	if (_modelViewer)
		_modelViewer->showOnlySelectedItems();
}

void FilterByBoundingBoxDialog::onHideClicked()
{
	if (_modelViewer)
		_modelViewer->hideSelectedItems();
}
