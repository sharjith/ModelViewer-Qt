#include "MeasurementDialog.h"
#include "ui_MeasurementDialog.h"

#include "ModelViewer.h"
#include "SceneGraph.h"
#include "ViewportWidget.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QFont>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardItemModel>

namespace
{
	// Same widget-hierarchy-walking rationale as RtRenderDialog's own
	// findMdiArea() - ModelViewer has no direct pointer to the QMdiArea that
	// contains it.
	QMdiArea* findMdiArea(QWidget* widget)
	{
		for (QWidget* w = widget; w; w = w->parentWidget())
		{
			if (auto* area = qobject_cast<QMdiArea*>(w))
				return area;
		}
		return nullptr;
	}

	// QListWidget's default behavior (in either SingleSelection or, now,
	// ExtendedSelection mode) treats a plain click on the sole already-
	// selected row as a no-op - it stays selected, there's no way to get
	// back to "nothing selected" except selecting a different row. This
	// list needs an explicit deselect (Delete should then fall back to
	// whatever's selected directly in the viewport, not whatever measurement
	// happened to be selected in the dialog last). Must intercept in
	// mousePressEvent BEFORE the base class updates the selection model - by
	// the time any of QListWidget's own selection signals fire (itemPressed,
	// itemClicked, itemSelectionChanged), the click has already been applied,
	// so there is no signal-based way to tell "this hit an already-selected
	// item" from "this click is what just selected it". Scoped to the
	// no-modifier, exactly-one-item-selected case only - a Ctrl/Shift click,
	// or a plain click on one item within a larger multi-selection (which
	// Qt's own ExtendedSelection handling correctly collapses to just that
	// item), both fall through to the base class unchanged.
	class MeasurementResultsList : public QListWidget
	{
	public:
		// dialog doubles as the QWidget parent (it always IS one, in
		// practice - see MeasurementDialog's own construction of this list)
		// and as the batch-toggle target keyPressEvent() below calls into,
		// since this list has no scene-graph access of its own.
		explicit MeasurementResultsList(MeasurementDialog* dialog) : QListWidget(dialog), _dialog(dialog) {}

	protected:
		void mousePressEvent(QMouseEvent* event) override
		{
			const QModelIndex index = indexAt(event->pos());
			if (event->button() == Qt::LeftButton && index.isValid() && event->modifiers() == Qt::NoModifier
				&& selectionModel() && selectionModel()->isSelected(index)
				&& selectionModel()->selectedIndexes().size() == 1)
			{
				clearSelection();
				setCurrentIndex(QModelIndex());
				return;
			}
			QListWidget::mousePressEvent(event);
		}

		// QAbstractItemView's own Space handling only toggles the CURRENT
		// item's check state, not every selected one - it treats Space as
		// "activate the current index", the same category of gesture as
		// Enter, not a batch operation over the whole selection. Extended
		// here so a multi-selection check/uncheck moves together in one
		// press, matching what a user pressing Space on several selected
		// rows actually expects. New state = whatever Space would have set
		// the CURRENT item to on its own (its opposite) - applied to every
		// selected, checkable row, so the outcome for the row under the
		// cursor is unchanged from stock behavior, just extended to its
		// selected neighbors. Delegates the actual application to
		// _dialog->toggleCheckStatesForSelection() rather than looping
		// item->setCheckState() calls here directly - see that method's doc
		// comment for why (each one would otherwise synchronously rebuild
		// this whole list mid-loop).
		void keyPressEvent(QKeyEvent* event) override
		{
			if (event->key() == Qt::Key_Space && selectionModel() && selectionModel()->selectedIndexes().size() > 1)
			{
				QListWidgetItem* current = currentItem();
				if (current && (current->flags() & Qt::ItemIsUserCheckable) && _dialog)
				{
					const Qt::CheckState newState = (current->checkState() == Qt::Checked) ? Qt::Unchecked : Qt::Checked;
					_dialog->toggleCheckStatesForSelection(newState);
					event->accept();
					return;
				}
			}
			QListWidget::keyPressEvent(event);
		}

	private:
		MeasurementDialog* _dialog;
	};
}

MeasurementDialog::MeasurementDialog(ModelViewer* modelViewer, QWidget* parent)
	: QDialog(parent)
	, _modelViewer(modelViewer)
	, ui(std::make_unique<Ui::MeasurementDialog>())
{
	ui->setupUi(this);
	setAttribute(Qt::WA_DeleteOnClose);

	// resultsList is a plain QListWidget placeholder in the .ui (Designer
	// controls its position/size there) - swapped here for the real
	// MeasurementResultsList subclass, which Designer can't construct
	// directly since it's a private, header-less class local to this file.
	// See ShrinkWrapDialog's own conversion for why this swap (rather than
	// Designer's "promote to" feature) was chosen.
	MeasurementResultsList* resultsList = new MeasurementResultsList(this);
	resultsList->setSelectionMode(QAbstractItemView::ExtendedSelection);
	ui->verticalLayout->replaceWidget(ui->resultsList, resultsList);
	delete ui->resultsList;
	ui->resultsList = resultsList;

	{
		// Grouped with non-selectable category headers rather than one flat
		// list - a dozen-plus similarly-named tools (EdgeToVertex/EdgeToEdge/
		// EdgeToFace and friends) stop being scannable as a plain dropdown
		// well before this many entries. Groups follow the tools' own
		// naming/picking-infrastructure families (see MeasurementData.h),
		// not "reports a distance" vs "reports an angle" - several tools
		// (Face to Face, Edge to Edge, Edge to Face) report either
		// depending on the picked geometry's own relative orientation, so
		// there's no distance-vs-angle split that would actually hold up.
		auto addGroupHeader = [this](const QString& title) {
			ui->toolCombo->addItem(title);
			if (auto* model = qobject_cast<QStandardItemModel*>(ui->toolCombo->model()))
			{
				QStandardItem* item = model->item(ui->toolCombo->count() - 1);
				item->setFlags(item->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsEnabled));
				QFont font = item->font();
				font.setBold(true);
				item->setFont(font);
			}
		};
		auto addTool = [this](MeasurementTool tool) {
			ui->toolCombo->addItem("    " + measurementToolDisplayName(tool), static_cast<int>(tool));
		};

		addGroupHeader(tr("Point & Distance"));
		addTool(MeasurementTool::Point);
		addTool(MeasurementTool::Distance);
		addTool(MeasurementTool::GeodesicDistance);
		addTool(MeasurementTool::AngleThreePoint);

		addGroupHeader(tr("Arcs & Circles"));
		addTool(MeasurementTool::ArcRadius3Point);
		addTool(MeasurementTool::ArcRadiusCenterPoint);
		addTool(MeasurementTool::EdgeRadius);
		addTool(MeasurementTool::PitchCircle);
		addTool(MeasurementTool::Concentricity);
		addTool(MeasurementTool::CylindricalDiameter);

		addGroupHeader(tr("Faces"));
		addTool(MeasurementTool::FaceToFace);
		addTool(MeasurementTool::PointToFace);
		addTool(MeasurementTool::FaceArea);
		addTool(MeasurementTool::MinDistance);

		addGroupHeader(tr("Edges"));
		addTool(MeasurementTool::EdgeLength);
		addTool(MeasurementTool::EdgeToVertex);
		addTool(MeasurementTool::EdgeToEdge);
		addTool(MeasurementTool::EdgeToFace);
		addTool(MeasurementTool::EdgeChain);

		// Index 0 is now a disabled header, not a real tool - the combo
		// otherwise defaults its current index there, which would leave no
		// tool armed at all when the dialog opens (see the currentIndex()
		// re-arm call at the end of this constructor).
		ui->toolCombo->setCurrentIndex(ui->toolCombo->findData(static_cast<int>(MeasurementTool::Point)));
	}
	// Surfaces the variable-pick-count workflow up front, since it's the one
	// tool in the list that doesn't auto-complete at a fixed number of
	// clicks (see MeasurementData.h's measurementToolHasVariableAnchorCount()).
	ui->toolCombo->setItemData(ui->toolCombo->findData(static_cast<int>(MeasurementTool::PitchCircle)),
		tr("Click every hole center around the pattern (3 or more, any order) - snaps to a circular edge's exact center same as Point/Distance. Press Enter or the Finish button once you've clicked them all."),
		Qt::ToolTipRole);
	// See MeasurementData.h's ArcRadiusCenterPoint doc comment - surfaced
	// here so the remaining glTF/OBJ limitation is discoverable without
	// reading code.
	ui->toolCombo->setItemData(ui->toolCombo->findData(static_cast<int>(MeasurementTool::ArcRadiusCenterPoint)),
		tr("On STEP/IGES/BREP parts, snaps to a circular edge's exact center (holes included). On glTF/OBJ meshes, the center must land on real geometry (e.g. a boss's flat cap face) - won't work for a through-hole's center, which is empty space"),
		Qt::ToolTipRole);
	// CAD-only (see MeasurementData.h's EdgeRadius doc comment) - glTF/OBJ
	// meshes have no OCC edge data, so nothing is ever pickable for them.
	// Works correctly for through-holes too, unlike Center + 2-Point above.
	ui->toolCombo->setItemData(ui->toolCombo->findData(static_cast<int>(MeasurementTool::EdgeRadius)),
		tr("STEP/IGES/BREP parts only - click directly on a circular edge (hole or boss). Not available for glTF/OBJ meshes."),
		Qt::ToolTipRole);
	// Same CAD-only pick as Edge Radius above (both circular-edge anchors).
	ui->toolCombo->setItemData(ui->toolCombo->findData(static_cast<int>(MeasurementTool::Concentricity)),
		tr("STEP/IGES/BREP parts only - click directly on two circular edges (holes or bosses) to compare their centers and axes. Not available for glTF/OBJ meshes."),
		Qt::ToolTipRole);
	// CAD faces use their exact analytic axis. glTF/OBJ meshes use a local
	// geometric fit and deliberately decline an ambiguous or non-round patch.
	ui->toolCombo->setItemData(ui->toolCombo->findData(static_cast<int>(MeasurementTool::CylindricalDiameter)),
		tr("Click directly on a cylindrical or conical curved surface (not its rim edge - see Edge Radius for that). STEP/IGES/BREP uses the exact surface axis; glTF/OBJ uses a validated local fit. Diameter varies along a cone's length."),
		Qt::ToolTipRole);
	// The other variable-pick-count tool alongside Pitch Circle above - same
	// note about it not auto-completing at a fixed click count.
	ui->toolCombo->setItemData(ui->toolCombo->findData(static_cast<int>(MeasurementTool::EdgeChain)),
		tr("Click a contiguous run of edges to sum (2 or more, each one must share an endpoint with the last) - works for an open chain (e.g. a weld seam) or a closed perimeter alike. An edge that doesn't connect is rejected. Press Enter or the Finish button once you've clicked them all."),
		Qt::ToolTipRole);
	// Each pick expands to its whole smooth face (bounded by real feature
	// edges, so a curved surface counts as one pick) - worth surfacing
	// since it's a bigger region than the single triangle every other face
	// pick in this dialog uses, and the closest-point search cost scales
	// with it.
	ui->toolCombo->setItemData(ui->toolCombo->findData(static_cast<int>(MeasurementTool::MinDistance)),
		tr("Click two faces (flat or curved - each pick expands to its whole smooth face, bounded by real edges) to find the true closest points between them. Works on the same part (e.g. a wall-thickness check) or two different ones. May take a moment on a very large, finely-tessellated face."),
		Qt::ToolTipRole);
	// Unlike Distance (which allows two different meshes), a geodesic path
	// only makes sense along ONE continuous surface - the second pick is
	// rejected if it lands on a different mesh (see
	// MeasurementController::handleMeasurementClick()'s same-mesh check).
	ui->toolCombo->setItemData(ui->toolCombo->findData(static_cast<int>(MeasurementTool::GeodesicDistance)),
		tr("Click two points on the SAME mesh - reports the distance ALONG the surface between them (e.g. wrapping around a curved part), not the straight-line distance. Both points must land on the same mesh."),
		Qt::ToolTipRole);

	connect(ui->toolCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MeasurementDialog::onToolComboChanged);
	connect(ui->resultsList, &QListWidget::itemSelectionChanged, this, &MeasurementDialog::onResultsSelectionChanged);
	connect(ui->resultsList, &QListWidget::itemChanged, this, &MeasurementDialog::onResultItemChanged);
	connect(ui->deleteButton, &QPushButton::clicked, this, &MeasurementDialog::onDeleteClicked);
	connect(ui->finishButton, &QPushButton::clicked, this, &MeasurementDialog::onFinishClicked);

	ViewportWidget* viewport = _modelViewer->getViewportWidget();
	connect(viewport, &ViewportWidget::measurementProgressChanged, this, &MeasurementDialog::onMeasurementProgressChanged);
	connect(viewport, &ViewportWidget::measurementToolChanged, this, &MeasurementDialog::onMeasurementToolChangedExternally);
	connect(viewport, &ViewportWidget::measurementSelectionChanged, this, &MeasurementDialog::onViewportSelectionChanged);
	if (SceneGraph* sceneGraph = _modelViewer->sceneGraph())
		connect(sceneGraph, &SceneGraph::measurementsChanged, this, &MeasurementDialog::onMeasurementsChanged);

	refreshResultsList();

	// Arms the first tool (Point) the instant the dialog opens - mirrors the
	// old checkable-action behavior where clicking "Measure Point" armed it
	// immediately, rather than making the user also pick from the combo
	// before anything happens.
	onToolComboChanged(ui->toolCombo->currentIndex());

	if (QMdiArea* mdiArea = findMdiArea(_modelViewer))
		connect(mdiArea, &QMdiArea::subWindowActivated, this, &MeasurementDialog::onActiveSubWindowChanged);

	loadSettings();  // restores window geometry, if any was saved - after the .ui's own geometry so it can override that default
}

MeasurementDialog::~MeasurementDialog()
{
}

void MeasurementDialog::closeEvent(QCloseEvent* event)
{
	// Disarm whatever tool this dialog left active - closing the dialog with
	// no way to see the pick-prompt or switch tools shouldn't leave clicks
	// in the viewport silently still placing measurement points.
	if (ViewportWidget* viewport = _modelViewer->getViewportWidget())
		viewport->setMeasurementTool(MeasurementTool::None);
	saveSettings();
	QDialog::closeEvent(event);
}

void MeasurementDialog::loadSettings()
{
	QSettings settings;
	const QByteArray geometry = settings.value("measurement/geometry", QByteArray()).toByteArray();
	if (!geometry.isEmpty())
		restoreGeometry(geometry);
}

void MeasurementDialog::saveSettings()
{
	QSettings settings;
	settings.setValue("measurement/geometry", saveGeometry());
}

void MeasurementDialog::onToolComboChanged(int index)
{
	if (index < 0)
		return;
	const MeasurementTool tool = static_cast<MeasurementTool>(ui->toolCombo->itemData(index).toInt());
	if (ViewportWidget* viewport = _modelViewer->getViewportWidget())
		viewport->setMeasurementTool(tool);
}

void MeasurementDialog::onMeasurementProgressChanged(int picked, int required)
{
	ViewportWidget* viewport = _modelViewer->getViewportWidget();
	if (!viewport)
		return;
	const MeasurementTool tool = viewport->measurementTool();
	ui->statusLabel->setText(measurementToolPickPrompt(tool, picked));

	// The Finish button only makes sense for a variable-anchor-count tool
	// (currently just Pitch Circle) - every other tool already auto-
	// completes the instant `required` is reached, so there's never
	// anything left to "finish" for them.
	const bool variableLength = measurementToolHasVariableAnchorCount(tool);
	ui->finishButton->setVisible(variableLength);
	ui->finishButton->setEnabled(variableLength && picked >= required);
}

void MeasurementDialog::onFinishClicked()
{
	if (ViewportWidget* viewport = _modelViewer->getViewportWidget())
		viewport->finishVariableLengthMeasurement();
}

void MeasurementDialog::onMeasurementToolChangedExternally(MeasurementTool tool)
{
	// Only Escape (inside ViewportWidget) can drive the tool to None from
	// outside this dialog - reflect that by parking the combo without
	// re-arming a tool via onToolComboChanged() (see setComboToolSilently()).
	if (tool == MeasurementTool::None)
		ui->statusLabel->setText(tr("Cancelled - pick a tool to resume"));
	else
		setComboToolSilently(tool);
}

void MeasurementDialog::setComboToolSilently(MeasurementTool tool)
{
	const QSignalBlocker blocker(ui->toolCombo);
	ui->toolCombo->setCurrentIndex(ui->toolCombo->findData(static_cast<int>(tool)));
}

void MeasurementDialog::onMeasurementsChanged()
{
	if (_batchingVisibilityChanges)
		return;  // toggleCheckStatesForSelection() will rebuild once, at the end of its own batch
	refreshResultsList();
}

void MeasurementDialog::toggleCheckStatesForSelection(Qt::CheckState newState)
{
	SceneGraph* sceneGraph = _modelViewer->sceneGraph();
	ViewportWidget* viewport = _modelViewer->getViewportWidget();
	if (!sceneGraph || !viewport)
		return;

	_batchingVisibilityChanges = true;
	for (QListWidgetItem* item : ui->resultsList->selectedItems())
	{
		if (!(item->flags() & Qt::ItemIsUserCheckable))
			continue;
		const QUuid id = item->data(Qt::UserRole).toUuid();
		sceneGraph->setMeasurementVisible(id, newState == Qt::Checked);
	}
	_batchingVisibilityChanges = false;

	refreshResultsList();
	viewport->update();
}

void MeasurementDialog::refreshResultsList()
{
	SceneGraph* sceneGraph = _modelViewer->sceneGraph();
	ViewportWidget* viewport = _modelViewer->getViewportWidget();
	if (!sceneGraph || !viewport)
		return;

	const QSet<QUuid> selectedIds = viewport->selectedMeasurementIds();

	_updatingSelectionFromViewport = true;
	ui->resultsList->clear();
	QListWidgetItem* firstSelectedItem = nullptr;
	for (const Measurement& m : sceneGraph->measurements())
	{
		QListWidgetItem* item = new QListWidgetItem(viewport->measurementSummaryText(m), ui->resultsList);
		item->setData(Qt::UserRole, m.id);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(m.visible ? Qt::Checked : Qt::Unchecked);
		if (selectedIds.contains(m.id))
		{
			item->setSelected(true);
			if (!firstSelectedItem)
				firstSelectedItem = item;
		}
	}
	// clear() drops Qt's own separate "current item" concept along with
	// every old item, and nothing above restores it (setSelected() only
	// restores the SELECTION, a distinct thing) - left unset, currentItem()
	// returns null after any rebuild, breaking anything that relies on it
	// afterward (e.g. MeasurementResultsList::keyPressEvent()'s Space
	// handling, and Qt's OWN stock Space handling, which is exactly why a
	// second Space press had no effect at all rather than falling back to
	// single-item behavior). NoUpdate so this only moves "current", without
	// perturbing the selection just restored above.
	if (firstSelectedItem)
		ui->resultsList->setCurrentItem(firstSelectedItem, QItemSelectionModel::NoUpdate);
	_updatingSelectionFromViewport = false;

	ui->deleteButton->setEnabled(!selectedIds.isEmpty());
}

void MeasurementDialog::onResultsSelectionChanged()
{
	if (_updatingSelectionFromViewport)
		return;

	ViewportWidget* viewport = _modelViewer->getViewportWidget();
	if (!viewport)
		return;

	QSet<QUuid> ids;
	for (QListWidgetItem* item : ui->resultsList->selectedItems())
		ids.insert(item->data(Qt::UserRole).toUuid());

	_updatingSelectionFromViewport = true;
	viewport->setSelectedMeasurementIds(ids);
	_updatingSelectionFromViewport = false;

	ui->deleteButton->setEnabled(!ids.isEmpty());
}

void MeasurementDialog::onViewportSelectionChanged(const QSet<QUuid>& ids)
{
	if (_updatingSelectionFromViewport)
		return;

	_updatingSelectionFromViewport = true;
	ui->resultsList->clearSelection();
	QListWidgetItem* firstMatch = nullptr;
	for (int i = 0; i < ui->resultsList->count(); ++i)
	{
		QListWidgetItem* item = ui->resultsList->item(i);
		if (ids.contains(item->data(Qt::UserRole).toUuid()))
		{
			item->setSelected(true);
			if (!firstMatch)
				firstMatch = item;
		}
	}
	if (firstMatch)
		ui->resultsList->scrollToItem(firstMatch);
	_updatingSelectionFromViewport = false;

	ui->deleteButton->setEnabled(!ids.isEmpty());
}

void MeasurementDialog::onResultItemChanged(QListWidgetItem* item)
{
	if (_updatingSelectionFromViewport || !item)
		return;

	SceneGraph* sceneGraph = _modelViewer->sceneGraph();
	ViewportWidget* viewport = _modelViewer->getViewportWidget();
	if (!sceneGraph)
		return;

	const QUuid id = item->data(Qt::UserRole).toUuid();
	sceneGraph->setMeasurementVisible(id, item->checkState() == Qt::Checked);

	// setMeasurementVisible() only emits measurementsChanged() (which this
	// dialog listens to for its OWN list, via onMeasurementsChanged()) - it
	// doesn't touch the GL viewport at all, so nothing else refreshes it.
	// refreshMeasurementAnnotationBounds() both repaints AND recomputes
	// bounds/conditionally re-fits (see its own doc comment) - a checkbox
	// toggle here is exactly the same kind of visibility change the
	// viewport context menu's Hide/Show already triggers it for.
	if (viewport)
		viewport->refreshMeasurementAnnotationBounds();
}

void MeasurementDialog::onDeleteClicked()
{
	if (_modelViewer)
		_modelViewer->deleteSelectedMeasurements();
}

void MeasurementDialog::onActiveSubWindowChanged(QMdiSubWindow* activeSubWindow)
{
	const bool isOwnDocumentActive = _modelViewer
		&& activeSubWindow
		&& activeSubWindow->widget() == static_cast<QWidget*>(_modelViewer);
	setVisible(isOwnDocumentActive);
}
