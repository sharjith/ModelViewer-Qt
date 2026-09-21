#include "MassPropertiesDialog.h"
#include "ModelViewer.h"
#include "ViewportWidget.h"
#include "SceneMesh.h"
#include "MeshProperties.h"
#include "Material.h"
#include "LengthUnits.h"
#include "AnalysisMeshSnapshot.h"
#include "AnalysisComputeSession.h"
#include "MeshSelectionEditor.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QIcon>
#include <QItemSelectionModel>
#include <QLabel>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QProgressBar>
#include <QStringList>
#include <QVector3D>
#include <QMap>
#include <QCloseEvent>
#include <QSettings>
#include <QJsonObject>
#include <any>

MassPropertiesDialog::MassPropertiesDialog(ModelViewer* modelViewer, QWidget* parent)
	: QDialog(parent)
	, _modelViewer(modelViewer)
{
	setWindowTitle(tr("Mass Properties"));
	resize(560, 420);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(16, 16, 16, 16);
	layout->setSpacing(12);

	auto* introLabel = new QLabel(tr("Volume, surface area, and mass for the selected meshes - "
	                                  "recomputed when the selection changes or on Recalculate."), this);
	introLabel->setWordWrap(true);
	layout->addWidget(introLabel);

	// Selection box: the same pick / edit / clear affordances as the Exploded View panel's "Select assembly or
	// meshes" box. The dialog is non-modal so meshes can be picked in the viewport while it is open.
	{
		auto* selectionRow = new QHBoxLayout();
		selectionRow->addWidget(new QLabel(tr("Selection:"), this));
		_selectionEdit = new QLineEdit(this);
		_selectionEdit->setReadOnly(true);
		_selectionEdit->setPlaceholderText(tr("Select meshes..."));
		_selectionEdit->setToolTip(tr("The meshes this report covers. Right-click to edit or clear."));
		_selectionEdit->setContextMenuPolicy(Qt::CustomContextMenu);
		connect(_selectionEdit, &QWidget::customContextMenuRequested, this, &MassPropertiesDialog::showSelectionContextMenu);
		selectionRow->addWidget(_selectionEdit, 1);

		const auto makeIconButton = [this](const QString& iconPath, const QString& tip, bool checkable) {
			auto* button = new QPushButton(this);
			button->setIcon(QIcon(iconPath));
			button->setFixedSize(28, 28);
			button->setCheckable(checkable);
			button->setToolTip(tip);
			return button;
		};
		_pickButton = makeIconButton(QStringLiteral(":/icons/res/select.png"),
			tr("Add meshes from the scene or tree, then click again to confirm"), true);
		connect(_pickButton, &QPushButton::toggled, this, &MassPropertiesDialog::onPickToggled);
		selectionRow->addWidget(_pickButton);
		_editSelectionButton = makeIconButton(QStringLiteral(":/icons/res/edit_selection.png"), tr("Edit Selection..."), false);
		connect(_editSelectionButton, &QPushButton::clicked, this, &MassPropertiesDialog::editSelection);
		selectionRow->addWidget(_editSelectionButton);
		_clearSelectionButton = makeIconButton(QStringLiteral(":/icons/res/clear.png"), tr("Clear Selection"), false);
		connect(_clearSelectionButton, &QPushButton::clicked, this, &MassPropertiesDialog::clearSelection);
		selectionRow->addWidget(_clearSelectionButton);
		layout->addLayout(selectionRow);
	}

	// A real per-document/per-import unit policy now exists (LengthUnits.h) -
	// text is refreshed per populate() once it knows whether any selected
	// mesh actually needed the Unknown->Millimeter fallback (see
	// resolveEffectiveImportUnit()'s own doc comment); this constructor-time
	// text is just a placeholder shown before the first populate() call
	// fills it in properly.
	_unitsNoteLabel = new QLabel(this);
	_unitsNoteLabel->setWordWrap(true);
	layout->addWidget(_unitsNoteLabel);

	_noSelectionLabel = new QLabel(tr("Nothing selected - select one or more meshes first."), this);
	_noSelectionLabel->setWordWrap(true);
	layout->addWidget(_noSelectionLabel);

	_table = new QTableWidget(this);
	_table->setColumnCount(5);
	_table->setHorizontalHeaderLabels({ tr("Mesh"), tr("Material"), tr("Volume (mm³)"), tr("Surface Area (mm²)"), tr("Mass (kg)") });
	// Every column is user-resizable (Interactive) - the Mesh column used to be Stretch, which cannot be dragged.
	// Widths start at sensible values and the last column takes up any slack when the dialog is widened.
	_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
	_table->horizontalHeader()->setStretchLastSection(true);
	_table->horizontalHeader()->setMinimumSectionSize(60);
	_table->setColumnWidth(0, 190);
	_table->setColumnWidth(1, 150);
	_table->setColumnWidth(2, 130);
	_table->setColumnWidth(3, 150);
	_table->verticalHeader()->setVisible(false);
	_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	// Rows are selectable so the right-click menu (Center Screen / Hide / Show) can act on several meshes at once.
	_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
	_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	_table->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(_table, &QWidget::customContextMenuRequested, this, &MassPropertiesDialog::showTableContextMenu);
	layout->addWidget(_table, 1);

	_totalsLabel = new QLabel(this);
	_totalsLabel->setWordWrap(true);
	_totalsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	layout->addWidget(_totalsLabel);

	// Per-material mass breakdown, in its OWN table rather than appended as
	// more lines onto _totalsLabel above - a selection spanning many
	// distinctly-named materials would otherwise keep growing that label
	// without bound and push the table/buttons off-screen. A QTableWidget
	// scrolls natively within whatever space this layout gives it, the same
	// way _table above already does, instead of forcing the dialog itself
	// to grow to fit every row.
	_materialBreakdownLabel = new QLabel(tr("Mass by Material:"), this);
	layout->addWidget(_materialBreakdownLabel);

	_materialTable = new QTableWidget(this);
	_materialTable->setColumnCount(2);
	_materialTable->setHorizontalHeaderLabels({ tr("Material"), tr("Mass (kg)") });
	_materialTable->horizontalHeader()->setStretchLastSection(true);
	_materialTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
	_materialTable->verticalHeader()->setVisible(false);
	_materialTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
	_materialTable->setSelectionMode(QAbstractItemView::NoSelection);
	layout->addWidget(_materialTable, 1);

	// Visible only while populate()'s background computation is in flight -
	// replaces the old wait-cursor-only feedback now that this genuinely
	// runs on a worker thread instead of blocking synchronously.
	_progressBar = new QProgressBar(this);
	_progressBar->setVisible(false);
	layout->addWidget(_progressBar);

	_closeButton = new QPushButton(tr("Close"), this);
	// Routed through a real slot, not directly to &QWidget::close - see
	// onCloseButtonClicked()'s own doc comment for why: it needs to cancel
	// instead of close while a computation is running.
	connect(_closeButton, &QPushButton::clicked, this, &MassPropertiesDialog::onCloseButtonClicked);
	_recalculateButton = new QPushButton(tr("Recalculate"), this);
	_recalculateButton->setToolTip(tr("Recompute the report - after editing a material, moving or changing a mesh"));
	connect(_recalculateButton, &QPushButton::clicked, this, [this]() { if (!_activeSession) populate(); });
	auto* buttonRow = new QHBoxLayout();
	buttonRow->addStretch(1);
	buttonRow->addWidget(_recalculateButton);
	buttonRow->addWidget(_closeButton);
	layout->addLayout(buttonRow);

	if (_modelViewer && _modelViewer->getViewportWidget())
	{
		connect(_modelViewer->getViewportWidget(), &ViewportWidget::meshAboutToBeDeleted,
			this, &MassPropertiesDialog::onMeshAboutToBeDeleted);
	}

	// Seed the mesh list from the viewport selection at the moment the dialog opens.
	if (_modelViewer && _modelViewer->getViewportWidget())
	{
		ViewportWidget* viewport = _modelViewer->getViewportWidget();
		for (int id : _modelViewer->getSelectedIDs())
		{
			const QUuid uuid = viewport->getUuidByIndex(id);
			if (!uuid.isNull() && !_meshUuids.contains(uuid))
				_meshUuids.append(uuid);
		}
	}
	updateSelectionDisplay();

	populate();
	loadSettings();
}

void MassPropertiesDialog::seedFromViewportSelection()
{
	if (_activeSession || !_modelViewer || !_modelViewer->getViewportWidget())
		return;
	ViewportWidget* viewport = _modelViewer->getViewportWidget();
	QVector<QUuid> uuids;
	for (int id : _modelViewer->getSelectedIDs())
	{
		const QUuid uuid = viewport->getUuidByIndex(id);
		if (!uuid.isNull() && !uuids.contains(uuid))
			uuids.append(uuid);
	}
	if (!uuids.isEmpty())
		applyMeshUuids(uuids);
}

void MassPropertiesDialog::applyMeshUuids(const QVector<QUuid>& uuids)
{
	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	_meshUuids.clear();
	for (const QUuid& uuid : uuids)
	{
		if (uuid.isNull() || _meshUuids.contains(uuid))
			continue;
		if (viewport && viewport->getIndexByUuid(uuid) < 0)
			continue; // no longer in the scene
		_meshUuids.append(uuid);
	}
	updateSelectionDisplay();
	populate();
}

QString MassPropertiesDialog::describeSelection() const
{
	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	if (_meshUuids.isEmpty() || !viewport)
		return QString();
	if (_meshUuids.size() == 1)
	{
		if (SceneMesh* mesh = viewport->getMeshByUuid(_meshUuids.first()))
			return mesh->getName();
	}
	return tr("%1 meshes").arg(_meshUuids.size());
}

void MassPropertiesDialog::updateSelectionDisplay()
{
	if (_selectionEdit)
		_selectionEdit->setText(describeSelection());
	if (_editSelectionButton)
		_editSelectionButton->setEnabled(!_meshUuids.isEmpty());
	if (_clearSelectionButton)
		_clearSelectionButton->setEnabled(!_meshUuids.isEmpty());
}

void MassPropertiesDialog::onPickToggled(bool checked)
{
	if (_activeSession)
	{
		QSignalBlocker blocker(_pickButton);
		_pickButton->setChecked(false);
		return;
	}
	if (checked)
	{
		// Picking happens in the viewport/tree: the dialog is non-modal so both stay usable.
		_selectionEdit->setPlaceholderText(tr("Add meshes, then click again to confirm..."));
		return;
	}

	_selectionEdit->setPlaceholderText(tr("Select meshes..."));
	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	if (!viewport)
		return;
	QVector<QUuid> merged = _meshUuids;
	for (int id : _modelViewer->getSelectedIDs())
	{
		const QUuid uuid = viewport->getUuidByIndex(id);
		if (!uuid.isNull() && !merged.contains(uuid))
			merged.append(uuid);
	}
	// The picked meshes now live in the list - clear the viewport selection so the next pick starts fresh.
	_modelViewer->setSelectionWithoutUndo(QSet<int>());
	applyMeshUuids(merged);
}

void MassPropertiesDialog::editSelection()
{
	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	if (_activeSession || !viewport || _meshUuids.isEmpty())
		return;

	MeshSelectionEditor editor(this);
	editor.setIntroText(tr("Review and refine the meshes in this report."));
	editor.setMembersText(tr("Meshes"));
	QVector<MeshSelectionEditor::Entry> entries;
	for (const QUuid& uuid : std::as_const(_meshUuids))
	{
		if (SceneMesh* mesh = viewport->getMeshByUuid(uuid))
			entries.append({ uuid, mesh->getName() });
	}
	editor.setEntries(entries);

	// Highlighting the entry under the cursor shows where that mesh is; the previous selection is put back afterwards.
	QSet<int> previousSelection;
	for (int id : _modelViewer->getSelectedIDs())
		previousSelection.insert(id);
	connect(&editor, &MeshSelectionEditor::previewEntryRequested, this, [this](const QUuid& uuid) {
		_modelViewer->setSelectionWithoutUndo(QSet<QUuid>{ uuid });
	});

	const int result = editor.exec();
	_modelViewer->setSelectionWithoutUndo(previousSelection);
	if (result != QDialog::Accepted && result != MeshSelectionEditor::AddMoreResult)
		return;

	QVector<QUuid> updated;
	for (const MeshSelectionEditor::Entry& entry : editor.entries())
		updated.append(entry.uuid);
	applyMeshUuids(updated);
	if (result == MeshSelectionEditor::AddMoreResult && _pickButton)
		_pickButton->setChecked(true);
}

void MassPropertiesDialog::clearSelection()
{
	if (_activeSession)
		return;
	if (_pickButton && _pickButton->isChecked())
	{
		QSignalBlocker blocker(_pickButton);
		_pickButton->setChecked(false);
		_selectionEdit->setPlaceholderText(tr("Select meshes..."));
	}
	applyMeshUuids(QVector<QUuid>());
}

void MassPropertiesDialog::showSelectionContextMenu(const QPoint& pos)
{
	if (_meshUuids.isEmpty())
		return;
	QMenu menu(this);
	connect(menu.addAction(QIcon(QStringLiteral(":/icons/res/edit_selection.png")), tr("Edit Selection...")),
		&QAction::triggered, this, &MassPropertiesDialog::editSelection);
	menu.addSeparator();
	connect(menu.addAction(QIcon(QStringLiteral(":/icons/res/clear.png")), tr("Clear Selection")),
		&QAction::triggered, this, &MassPropertiesDialog::clearSelection);
	menu.exec(_selectionEdit->mapToGlobal(pos));
}

QVector<QUuid> MassPropertiesDialog::meshesOfSelectedRows() const
{
	QVector<QUuid> uuids;
	if (!_table || !_table->selectionModel())
		return uuids;
	const QModelIndexList rows = _table->selectionModel()->selectedRows();
	for (const QModelIndex& index : rows)
	{
		const int row = index.row();
		if (row >= 0 && row < _rowUuids.size() && !_rowUuids[row].isNull() && !uuids.contains(_rowUuids[row]))
			uuids.append(_rowUuids[row]);
	}
	return uuids;
}

void MassPropertiesDialog::showTableContextMenu(const QPoint& pos)
{
	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	if (_activeSession || !viewport)
		return;

	// Right-clicking a row that is not part of the selection selects just that row - the same rule the scene tree
	// and most item views follow.
	const QModelIndex clicked = _table->indexAt(pos);
	if (!clicked.isValid())
		return;
	if (!_table->selectionModel()->isRowSelected(clicked.row(), QModelIndex()))
	{
		_table->clearSelection();
		_table->selectRow(clicked.row());
	}

	const QVector<QUuid> targets = meshesOfSelectedRows();
	std::vector<int> ids;
	QSet<QUuid> targetSet;
	for (const QUuid& uuid : targets)
	{
		const int id = viewport->getIndexByUuid(uuid);
		if (id >= 0)
		{
			ids.push_back(id);
			targetSet.insert(uuid);
		}
	}
	if (ids.empty())
		return;

	QMenu menu(this);
	connect(menu.addAction(QIcon(QStringLiteral(":/icons/res/center_screen.png")), tr("Center Screen")), &QAction::triggered, this,
		[viewport, ids]() { viewport->centerScreen(ids); });
	menu.addSeparator();
	connect(menu.addAction(QIcon(QStringLiteral(":/icons/res/hide.png")), tr("Hide")), &QAction::triggered, this,
		[this, targetSet]() {
			QSet<QUuid> visible = _modelViewer->getVisibleUuids();
			visible.subtract(targetSet);
			_modelViewer->setVisibilityWithUndo(visible, tr("Hide"));
		});
	connect(menu.addAction(QIcon(QStringLiteral(":/icons/res/show.png")), tr("Show")), &QAction::triggered, this,
		[this, targetSet]() {
			QSet<QUuid> visible = _modelViewer->getVisibleUuids();
			visible.unite(targetSet);
			_modelViewer->setVisibilityWithUndo(visible, tr("Show"));
		});
	menu.exec(_table->viewport()->mapToGlobal(pos));
}

void MassPropertiesDialog::onCloseButtonClicked()
{
	if (_activeSession)
	{
		_activeSession->requestCancel();
		return;
	}
	close();
}

void MassPropertiesDialog::onMeshAboutToBeDeleted(SceneMesh* mesh)
{
	if (mesh && _activeSession)
		_deletedWhileComputing.insert(mesh);
}

void MassPropertiesDialog::setComputationInFlight(bool inFlight)
{
	if (_closeButton)
		_closeButton->setText(inFlight ? tr("Cancel") : tr("Close"));
	for (QPushButton* button : { _pickButton, _editSelectionButton, _clearSelectionButton, _recalculateButton })
	{
		if (button)
			button->setEnabled(!inFlight && (button == _pickButton || button == _recalculateButton || !_meshUuids.isEmpty()));
	}
	if (_progressBar)
	{
		_progressBar->setVisible(inFlight);
		if (inFlight)
		{
			_progressBar->setRange(0, 0); // indeterminate until the first progress() signal reports a real total
			_progressBar->setValue(0);
		}
	}
}

void MassPropertiesDialog::closeEvent(QCloseEvent* event)
{
	// ModelViewer::openMassPropertiesDialog() creates this with WA_DeleteOnClose - letting it close (and delete
	// itself) while populate()'s runBlocking() call is still on the stack would return control to a frame whose
	// `this` is gone, the dangling-frame hazard AnalysisComputeSession's own doc comment describes. Redirect to
	// Cancel and refuse to close instead.
	if (_activeSession)
	{
		_activeSession->requestCancel();
		event->ignore();
		return;
	}

	saveSettings();
	QDialog::closeEvent(event);
}

void MassPropertiesDialog::reject()
{
	// Same reasoning as closeEvent() above - Escape doesn't route through
	// closeEvent() (QDialog::reject() only hide()s).
	if (_activeSession)
	{
		_activeSession->requestCancel();
		return;
	}

	saveSettings();
	QDialog::reject();
}

void MassPropertiesDialog::loadSettings()
{
	QSettings settings;
	const QByteArray geometry = settings.value("massProperties/geometry", QByteArray()).toByteArray();
	if (!geometry.isEmpty())
		restoreGeometry(geometry);
}

void MassPropertiesDialog::saveSettings()
{
	QSettings settings;
	settings.setValue("massProperties/geometry", saveGeometry());
}

void MassPropertiesDialog::populate()
{
	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	if (!viewport)
		return;

	// The dialog's own list (not the live viewport selection): the table rows follow it, in order.
	std::vector<int> selected;
	_rowUuids.clear();
	for (const QUuid& uuid : std::as_const(_meshUuids))
	{
		const int id = viewport->getIndexByUuid(uuid);
		if (id < 0)
			continue; // deleted since it was added
		selected.push_back(id);
		_rowUuids.append(uuid);
	}
	_noSelectionLabel->setVisible(selected.empty());
	if (selected.empty())
		_unitsNoteLabel->clear();
	_table->setVisible(!selected.empty());
	_totalsLabel->setVisible(!selected.empty());
	_materialBreakdownLabel->setVisible(false);
	_materialTable->setVisible(false);
	_materialTable->setRowCount(0);
	if (selected.empty())
		return;

	const std::vector<SceneMesh*> meshStore = viewport->getMeshStore();
	_table->setRowCount(static_cast<int>(selected.size()));

	// Captured up front (main thread), before dispatch, aligned by index
	// with the snapshot list below - each mesh's own material density and
	// name. Re-reading these AFTER the computation instead would open a new
	// race window this dialog's original fully-synchronous code never had
	// to consider (the mesh's material could be edited mid-computation);
	// capturing once here, matching AnalysisMeshSnapshot's own "copy out
	// before dispatch" principle, avoids it.
	QVariantMap params;
	params.insert(QStringLiteral("mode"), QStringLiteral("massProperties"));
	std::vector<AnalysisMeshSnapshot> snapshots;
	std::vector<float> densityByIndex;
	std::vector<float> shellThicknessByIndex; // mm; <= 0 = unset - see Material::shellThickness()
	std::vector<QString> materialNameByIndex;
	snapshots.reserve(selected.size());
	densityByIndex.reserve(selected.size());
	shellThicknessByIndex.reserve(selected.size());
	materialNameByIndex.reserve(selected.size());
	for (int id : selected)
	{
		SceneMesh* mesh = meshStore.at(id);
		snapshots.push_back(captureAnalysisMeshSnapshot(mesh, params));
		// Sourced from THIS mesh's own material - no fallback density, ever
		// (see Material::hasDensity()'s own doc comment on why a material
		// with no known density must never silently default to one).
		const Material meshMaterial = mesh->getMaterial();
		densityByIndex.push_back(meshMaterial.hasDensity() ? meshMaterial.density() : -1.0f);
		shellThicknessByIndex.push_back(meshMaterial.hasShellThickness() ? meshMaterial.shellThickness() : -1.0f);
		materialNameByIndex.push_back(meshMaterial.name());
	}

	_deletedWhileComputing.clear();
	setComputationInFlight(true);

	AnalysisComputeSession session(this);
	_activeSession = &session;

	// Cheap interim mitigation replaced: MeshProperties' CGAL topology
	// checks (is_closed/does_self_intersect/does_bound_a_volume) used to run
	// synchronously on the UI thread here and could take a noticeable
	// moment on a CAD-sized selection - now genuinely backgrounded, with a
	// real progress bar and Cancel support.
	const std::vector<AnalysisComputeSession::PerMeshOutcome> outcomes = session.runBlocking(
		std::move(snapshots),
		[](const AnalysisMeshSnapshot& snapshot) -> std::any
		{
			return computeMeshGeometry(snapshot.points, snapshot.indices, snapshot.boundingBox);
		},
		[this](int processed, int total)
		{
			if (_progressBar)
			{
				_progressBar->setRange(0, total);
				_progressBar->setValue(processed);
			}
		});

	_activeSession = nullptr;
	setComputationInFlight(false);

	if (outcomes.empty())
	{
		// Cancelled - AnalysisComputeSession never half-applies a batch, and
		// this dialog's own "recomputed fresh every time it opens" contract
		// means there's no earlier valid state to fall back to either, so
		// there's nothing left to show.
		_table->setVisible(false);
		_totalsLabel->setVisible(false);
		return;
	}

	double knownSurfaceAreaSubtotal = 0.0;
	int surfaceAreaExcludedCount = 0;
	QStringList surfaceAreaExclusionReasons;

	double knownVolumeSubtotal = 0.0;
	int volumeExcludedCount = 0;
	QStringList volumeExclusionReasons;
	bool allHaveVolume = true;
	QVector3D volumeWeightedCentroidAccum;
	double volumeWeightSum = 0.0;

	double knownMassSubtotal = 0.0;
	int massExcludedCount = 0;
	QStringList massExclusionReasons;
	bool allHaveMass = true;
	QVector3D massWeightedCentroidAccum;
	double massWeightSum = 0.0;

	// Mass rollup grouped by material NAME - the only identity Material
	// exposes (it carries no UUID), so two differently-configured materials
	// that happen to share a display name are indistinguishable here; a
	// known, documented limitation (per Step 8's plan) rather than a silent
	// inaccuracy. Computed per-mesh, before grouping - each mesh's mass
	// comes from ITS OWN material's density, never a group-level density
	// applied after the fact, so two same-named-but-differently-configured
	// materials never silently share one density value.
	struct MaterialMassGroup
	{
		double knownMassSubtotal = 0.0;
		int excludedCount = 0;
		int totalCount = 0;
		QStringList exclusionReasons;
	};
	QMap<QString, MaterialMassGroup> massByMaterial;

	// Counts how many selected meshes resolved via the Unknown->Millimeter
	// fallback (see resolveEffectiveImportUnit()'s own doc comment) rather
	// than a real, explicitly-known unit - drives _unitsNoteLabel's text
	// below.
	int unitFallbackCount = 0;

	// Meshes whose open surfaces were counted as area x shell thickness, and meshes excluded that a shell
	// thickness on their material WOULD have included - both drive a footnote below.
	int shellMeshCount = 0;
	int shellCapableExcludedCount = 0;

	// resolveEffectiveImportUnit() takes viewerState as a QJsonObject (the
	// same shape it's persisted in) rather than a bare LengthUnit, so a
	// document's own defaultImportUnit is wrapped once here rather than
	// re-wrapped per mesh inside the loop below.
	QJsonObject documentViewerState;
	if (_modelViewer->defaultImportUnit() != LengthUnit::Unknown)
		documentViewerState.insert(QStringLiteral("defaultImportUnit"), lengthUnitToString(_modelViewer->defaultImportUnit()));

	for (size_t i = 0; i < outcomes.size(); ++i)
	{
		const AnalysisComputeSession::PerMeshOutcome& outcome = outcomes[i];
		SceneMesh* mesh = outcome.meshHandle;
		const int row = static_cast<int>(i);

		if (!mesh || _deletedWhileComputing.contains(mesh))
		{
			// Deleted while this ran - see _deletedWhileComputing's own doc
			// comment. Nothing safe to show for this row at all (not even a
			// name), and it can't contribute to any total/group.
			const QString reason = tr("mesh no longer available");
			_table->setItem(row, 0, new QTableWidgetItem(tr("(deleted)")));
			_table->setItem(row, 1, new QTableWidgetItem(QStringLiteral("-")));
			_table->setItem(row, 2, new QTableWidgetItem(tr("N/A (%1)").arg(reason)));
			_table->setItem(row, 3, new QTableWidgetItem(tr("N/A (%1)").arg(reason)));
			_table->setItem(row, 4, new QTableWidgetItem(tr("N/A (%1)").arg(reason)));
			++surfaceAreaExcludedCount;
			++volumeExcludedCount;
			++massExcludedCount;
			allHaveVolume = false;
			allHaveMass = false;
			if (!surfaceAreaExclusionReasons.contains(reason)) surfaceAreaExclusionReasons.append(reason);
			if (!volumeExclusionReasons.contains(reason)) volumeExclusionReasons.append(reason);
			if (!massExclusionReasons.contains(reason)) massExclusionReasons.append(reason);
			continue;
		}

		_table->setItem(row, 0, new QTableWidgetItem(mesh->getName()));
		{
			// The material applied to this mesh, so its density / shell thickness source is visible right here.
			const QString materialName = materialNameByIndex[i].isEmpty() ? QStringLiteral("-") : materialNameByIndex[i];
			auto* materialItem = new QTableWidgetItem(materialName);
			materialItem->setToolTip(materialName);
			_table->setItem(row, 1, materialItem);
		}

		// Volume/surface-area/mass are all invariant under a rigid
		// translation/rotation, so unlike SurfaceAnalysisDialog's overlays
		// (which ARE transform-sensitive - Draft Angle depends on world-
		// space orientation) only a genuine GEOMETRY rebuild mid-computation
		// (undo, a boolean op, subdivision, ...) invalidates this row's
		// result - a pure transform change does not. Checked directly
		// against geometryRevision rather than the full
		// SurfaceAnalysisOverlay::computeCurrentKey()/CacheKey machinery
		// (which also compares transform - not the right check for a
		// quantity that doesn't depend on it).
		const bool geometryStale = mesh->geometryRevision() != outcome.snapshotKey.geometryRevision;
		const MeshGeometryComputeResult* result = geometryStale ? nullptr : std::any_cast<MeshGeometryComputeResult>(&outcome.result);
		if (!result)
		{
			const QString reason = tr("geometry changed during computation");
			_table->setItem(row, 2, new QTableWidgetItem(tr("N/A (%1)").arg(reason)));
			_table->setItem(row, 3, new QTableWidgetItem(tr("N/A (%1)").arg(reason)));
			_table->setItem(row, 4, new QTableWidgetItem(tr("N/A (%1)").arg(reason)));
			++surfaceAreaExcludedCount;
			++volumeExcludedCount;
			++massExcludedCount;
			allHaveVolume = false;
			allHaveMass = false;
			if (!surfaceAreaExclusionReasons.contains(reason)) surfaceAreaExclusionReasons.append(reason);
			if (!volumeExclusionReasons.contains(reason)) volumeExclusionReasons.append(reason);
			if (!massExclusionReasons.contains(reason)) massExclusionReasons.append(reason);
			continue;
		}

		const float density = densityByIndex[i];
		const QString materialName = materialNameByIndex[i];

		// Real per-document/per-import unit resolution, replacing the
		// previous hardcoded millimetre assumption - see LengthUnits.h's
		// own doc comment for the resolution order. computeMeshGeometry()
		// returns its result in the mesh's OWN native coordinate units;
		// surfaceArea scales by lengthScale^2, volume by lengthScale^3,
		// centerOfMass by lengthScale^1. Every existing scene/MVF file
		// still resolves to Millimeter (lengthScale == 1.0) today - nothing
		// sets SceneNode::importUnit or a document-level default yet (Step 9
		// of this app's units-policy plan, the scene-tree "Import Units..."
		// action, is not built) - so this is exact-behavior-preserving until
		// then, not a silent behavior change.
		const ResolvedLengthUnit resolvedUnit = resolveEffectiveImportUnit(mesh, _modelViewer->sceneGraph(), documentViewerState);
		if (!resolvedUnit.wasExplicit)
			++unitFallbackCount;
		const double lengthScale = lengthUnitToMillimeters(resolvedUnit.unit);
		const float scaledSurfaceArea = result->surfaceArea * static_cast<float>(lengthScale * lengthScale);
		// Solid pieces use their real volume; open-surface pieces count as area x the material's shell
		// thickness when one is set (see summarizeMeshVolume()). Everything below - the volume column, the
		// totals, mass and the centroids - reads this summary rather than the raw geometry result.
		const MeshVolumeSummary volumeSummary = summarizeMeshVolume(*result, lengthScale, shellThicknessByIndex[i]);
		const double scaledVolume = volumeSummary.volume;
		const QVector3D scaledCenterOfMass = volumeSummary.centerOfMass;

		// hasValidGeometry gates this the same way hasValidVolume/hasMass
		// gate the other two columns below - surfaceArea reads 0.0f (not a
		// real zero-area result) whenever the per-triangle scan itself
		// failed (invalid/degenerate indices, or threw), and that must
		// never be silently summed into the total as if it were a
		// legitimate answer.
		if (result->hasValidGeometry)
		{
			_table->setItem(row, 3, new QTableWidgetItem(QString::number(scaledSurfaceArea, 'f', 2)));
			knownSurfaceAreaSubtotal += scaledSurfaceArea;
		}
		else
		{
			const QString reason = describeMeshPropertyUnavailableReason(result->volumeUnavailableReason);
			_table->setItem(row, 3, new QTableWidgetItem(tr("N/A (%1)").arg(reason)));
			++surfaceAreaExcludedCount;
			if (!surfaceAreaExclusionReasons.contains(reason))
				surfaceAreaExclusionReasons.append(reason);
		}

		if (volumeSummary.valid)
		{
			QString volumeText = QString::number(scaledVolume, 'f', 2);
			auto* volumeItem = new QTableWidgetItem();
			if (volumeSummary.shellPieceCount > 0)
			{
				volumeText = tr("%1 (incl. shell)").arg(volumeText);
				volumeItem->setToolTip(tr("%1 mm³ of this volume is open surface area x the material's shell thickness (%2 open piece(s)).")
					.arg(volumeSummary.shellVolume, 0, 'f', 2).arg(volumeSummary.shellPieceCount));
				++shellMeshCount;
			}
			volumeItem->setText(volumeText);
			_table->setItem(row, 2, volumeItem);
			knownVolumeSubtotal += scaledVolume;
			volumeWeightedCentroidAccum += scaledCenterOfMass * static_cast<float>(scaledVolume);
			volumeWeightSum += scaledVolume;
		}
		else
		{
			if (volumeSummary.shellCapable)
				++shellCapableExcludedCount;
			const QString reason = describeMeshPropertyUnavailableReason(volumeSummary.reason);
			_table->setItem(row, 2, new QTableWidgetItem(tr("N/A (%1)").arg(reason)));
			++volumeExcludedCount;
			if (!volumeExclusionReasons.contains(reason))
				volumeExclusionReasons.append(reason);
			allHaveVolume = false;
		}

		MaterialMassGroup& group = massByMaterial[materialName];
		++group.totalCount;

		if (meshHasMass(volumeSummary.valid, density))
		{
			const float weight = computeMeshWeight(scaledVolume, density);
			_table->setItem(row, 4, new QTableWidgetItem(QString::number(weight, 'f', 3)));
			knownMassSubtotal += weight;
			massWeightedCentroidAccum += scaledCenterOfMass * weight;
			massWeightSum += weight;
			group.knownMassSubtotal += weight;
		}
		else
		{
			// A mesh with no valid volume has no mass for that SAME reason
			// (there's nothing to multiply a density by) - only surface the
			// distinct "no density assigned" reason when volume itself was
			// actually fine.
			const QString reason = volumeSummary.valid
				? describeMeshPropertyUnavailableReason(MeshPropertyUnavailableReason::MissingDensity)
				: describeMeshPropertyUnavailableReason(volumeSummary.reason);
			_table->setItem(row, 4, new QTableWidgetItem(tr("N/A (%1)").arg(reason)));
			++massExcludedCount;
			if (!massExclusionReasons.contains(reason))
				massExclusionReasons.append(reason);
			allHaveMass = false;

			++group.excludedCount;
			if (!group.exclusionReasons.contains(reason))
				group.exclusionReasons.append(reason);
		}
	}

	// Only warn about an unverified unit assumption when at least one
	// selected mesh actually needed the fallback - a mesh with a real,
	// explicitly-known unit (once Steps 8/9 let one be set) has nothing to
	// be unsure about. The density/typical-value disclosure below is always
	// shown regardless, since it's a real, permanent characteristic of the
	// catalog data, not a resolvable "unknown" state the way units are.
	// NOTE: no in-app way to correct this yet (Step 9 of this app's own
	// units-policy plan - a scene-tree "Import Units..." action - is not
	// built), so the note only discloses the assumption; it doesn't point
	// the user at a remedy that doesn't exist.
	QString unitsNote;
	if (unitFallbackCount > 0)
	{
		unitsNote = tr("%1 of %2 mesh(es) use an unconfirmed default unit (millimetre) - treat length-"
		               "based results as provisional until this can be corrected per-import. ")
			.arg(unitFallbackCount).arg(outcomes.size());
	}
	unitsNote += tr("Density comes from each mesh's assigned material; library-supplied values are typical/"
	                 "nominal figures for a generic grade, not an exact spec - verify before relying on Mass "
	                 "for an engineering-critical calculation.");
	if (shellMeshCount > 0)
	{
		unitsNote += QLatin1Char(' ') + tr("%1 mesh(es) include open surfaces counted as area x the material's shell thickness - "
		                                    "a pseudo volume, not an enclosed one.").arg(shellMeshCount);
	}
	if (shellCapableExcludedCount > 0)
	{
		unitsNote += QLatin1Char(' ') + tr("%1 mesh(es) are open surfaces and were excluded - set a Shell thickness on their "
		                                    "material (Materials > Physical Properties) to include them.").arg(shellCapableExcludedCount);
	}
	_unitsNoteLabel->setText(unitsNote);

	// Totals convention, applied identically to volume and mass (and to
	// every future aggregate this app ever adds alongside them): a complete
	// real total only when every contributing mesh had a valid value;
	// otherwise a clearly-labeled known-value subtotal plus an explicit
	// excluded-mesh count and reasons - never a partial sum silently
	// presented as if it were the whole selection's total.
	QString totals;
	if (surfaceAreaExcludedCount == 0)
		totals += tr("Surface Area: %1 mm²\n").arg(knownSurfaceAreaSubtotal, 0, 'f', 2);
	else
		totals += tr("Surface Area: %1 mm² known (%2 of %3 mesh(es) excluded - %4)\n")
			.arg(knownSurfaceAreaSubtotal, 0, 'f', 2).arg(surfaceAreaExcludedCount).arg(selected.size())
			.arg(surfaceAreaExclusionReasons.join(QStringLiteral(", ")));

	if (volumeExcludedCount == 0)
		totals += tr("Volume: %1 mm³\n").arg(knownVolumeSubtotal, 0, 'f', 2);
	else
		totals += tr("Volume: %1 mm³ known (%2 of %3 mesh(es) excluded - %4)\n")
			.arg(knownVolumeSubtotal, 0, 'f', 2).arg(volumeExcludedCount).arg(selected.size())
			.arg(volumeExclusionReasons.join(QStringLiteral(", ")));

	// Mass is sourced entirely from each mesh's own material's density
	// (Step 8) - MeshProperties never defaults density to a fake value, so
	// a mesh whose material has no assigned density (or no material at
	// all) correctly reads as excluded here rather than silently using the
	// old hardcoded-1000-kg/m^3 estimate this dialog's data used to compute
	// before Step 8.
	if (massExcludedCount == 0)
		totals += tr("Mass: %1 kg\n").arg(knownMassSubtotal, 0, 'f', 3);
	else
		totals += tr("Mass: %1 kg known (%2 of %3 mesh(es) excluded - %4)\n")
			.arg(knownMassSubtotal, 0, 'f', 3).arg(massExcludedCount).arg(selected.size())
			.arg(massExclusionReasons.join(QStringLiteral(", ")));

	// Per-material breakdown, same known-subtotal + excluded-count + reasons
	// convention as the assembly-level totals above - a group with some but
	// not all contributing meshes lacking density shows its OWN subtotal and
	// exclusions, never a bare "-" the way an earlier draft of this dialog
	// would have. Grouped by material NAME (see massByMaterial's own doc
	// comment on why - Material carries no UUID).
	//
	// Rendered into its own scrollable table (_materialTable), not appended
	// as more text onto _totalsLabel - a selection spanning many distinctly-
	// named materials must not keep growing a plain label without bound and
	// push the rest of the dialog off-screen (P2 Codex fix).
	_materialBreakdownLabel->setVisible(!massByMaterial.isEmpty());
	_materialTable->setVisible(!massByMaterial.isEmpty());
	_materialTable->setRowCount(static_cast<int>(massByMaterial.size()));
	int materialRow = 0;
	for (auto it = massByMaterial.constBegin(); it != massByMaterial.constEnd(); ++it)
	{
		const MaterialMassGroup& group = it.value();
		auto* nameItem = new QTableWidgetItem(it.key());
		nameItem->setToolTip(it.key());
		_materialTable->setItem(materialRow, 0, nameItem);
		const QString massText = (group.excludedCount == 0)
			? tr("%1 kg").arg(group.knownMassSubtotal, 0, 'f', 3)
			: tr("%1 kg known (%2 of %3 mesh(es) excluded - %4)")
				.arg(group.knownMassSubtotal, 0, 'f', 3)
				.arg(group.excludedCount).arg(group.totalCount)
				.arg(group.exclusionReasons.join(QStringLiteral(", ")));
		auto* massItem = new QTableWidgetItem(massText);
		// A mixed-validity group's exclusion-reasons list can run longer than
		// the column is wide (elided text in a fixed-height row silently hides
		// which reasons are involved) - the tooltip always carries the full,
		// un-elided string regardless of column width.
		massItem->setToolTip(massText);
		_materialTable->setItem(materialRow, 1, massItem);
		++materialRow;
	}

	// Geometric centroid: uniform-density-assumption centroid, volume-
	// weighted across the selection - available whenever every mesh has a
	// valid volume, independent of density/mass entirely.
	if (allHaveVolume && volumeWeightSum > 0.0)
	{
		const QVector3D centroid = volumeWeightedCentroidAccum / static_cast<float>(volumeWeightSum);
		totals += tr("Geometric Centroid: X %1, Y %2, Z %3\n")
			.arg(centroid.x(), 0, 'f', 3).arg(centroid.y(), 0, 'f', 3).arg(centroid.z(), 0, 'f', 3);
	}
	else
	{
		totals += tr("Geometric Centroid: N/A (%1)\n")
			.arg(allHaveVolume ? tr("selection has zero total volume") : tr("not every mesh has a valid volume"));
	}

	// True mass-weighted center of mass - NOT the same thing as the
	// geometric centroid above, and only meaningful once every mesh in the
	// selection has a real, known mass (see meshHasMass()'s doc comment on
	// why weighting by a fake/default density would be physically
	// meaningless). Also guards the zero-total-mass case
	// explicitly - density==0 is a legitimately accepted real value, but an
	// all-zero-mass selection has no well-defined mass-weighted centroid
	// (would need to divide by zero), independent of "mass" itself being
	// perfectly well-known.
	if (allHaveMass && massWeightSum > 0.0)
	{
		const QVector3D centroid = massWeightedCentroidAccum / static_cast<float>(massWeightSum);
		totals += tr("Mass-Weighted Center of Mass: X %1, Y %2, Z %3")
			.arg(centroid.x(), 0, 'f', 3).arg(centroid.y(), 0, 'f', 3).arg(centroid.z(), 0, 'f', 3);
	}
	else
	{
		totals += tr("Mass-Weighted Center of Mass: N/A (%1)")
			.arg(!allHaveMass ? tr("not every mesh has a known mass") : tr("selection has zero total mass"));
	}

	_totalsLabel->setText(totals);
}
