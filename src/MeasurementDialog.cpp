#include "MeasurementDialog.h"

#include "ModelViewer.h"
#include "SceneGraph.h"
#include "ViewportWidget.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QFont>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QVBoxLayout>

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
		explicit MeasurementResultsList(QWidget* parent) : QListWidget(parent) {}

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
	};
}

MeasurementDialog::MeasurementDialog(ModelViewer* modelViewer, QWidget* parent)
	: QDialog(parent)
	, _modelViewer(modelViewer)
{
	setWindowTitle(tr("Measure"));
	setAttribute(Qt::WA_DeleteOnClose);

	_toolCombo = new QComboBox(this);
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
			_toolCombo->addItem(title);
			if (auto* model = qobject_cast<QStandardItemModel*>(_toolCombo->model()))
			{
				QStandardItem* item = model->item(_toolCombo->count() - 1);
				item->setFlags(item->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsEnabled));
				QFont font = item->font();
				font.setBold(true);
				item->setFont(font);
			}
		};
		auto addTool = [this](MeasurementTool tool) {
			_toolCombo->addItem("    " + measurementToolDisplayName(tool), static_cast<int>(tool));
		};

		addGroupHeader(tr("Point & Distance"));
		addTool(MeasurementTool::Point);
		addTool(MeasurementTool::Distance);
		addTool(MeasurementTool::AngleThreePoint);

		addGroupHeader(tr("Arcs & Circles"));
		addTool(MeasurementTool::ArcRadius3Point);
		addTool(MeasurementTool::ArcRadiusCenterPoint);
		addTool(MeasurementTool::EdgeRadius);
		addTool(MeasurementTool::PitchCircle);
		addTool(MeasurementTool::Concentricity);

		addGroupHeader(tr("Faces"));
		addTool(MeasurementTool::FaceToFace);
		addTool(MeasurementTool::PointToFace);

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
		_toolCombo->setCurrentIndex(_toolCombo->findData(static_cast<int>(MeasurementTool::Point)));
	}
	// Surfaces the variable-pick-count workflow up front, since it's the one
	// tool in the list that doesn't auto-complete at a fixed number of
	// clicks (see MeasurementData.h's measurementToolHasVariableAnchorCount()).
	_toolCombo->setItemData(_toolCombo->findData(static_cast<int>(MeasurementTool::PitchCircle)),
		tr("Click every hole center around the pattern (3 or more, any order) - snaps to a circular edge's exact center same as Point/Distance. Press Enter or the Finish button once you've clicked them all."),
		Qt::ToolTipRole);
	// See MeasurementData.h's ArcRadiusCenterPoint doc comment - surfaced
	// here so the remaining glTF/OBJ limitation is discoverable without
	// reading code.
	_toolCombo->setItemData(_toolCombo->findData(static_cast<int>(MeasurementTool::ArcRadiusCenterPoint)),
		tr("On STEP/IGES/BREP parts, snaps to a circular edge's exact center (holes included). On glTF/OBJ meshes, the center must land on real geometry (e.g. a boss's flat cap face) - won't work for a through-hole's center, which is empty space"),
		Qt::ToolTipRole);
	// CAD-only (see MeasurementData.h's EdgeRadius doc comment) - glTF/OBJ
	// meshes have no OCC edge data, so nothing is ever pickable for them.
	// Works correctly for through-holes too, unlike Center + 2-Point above.
	_toolCombo->setItemData(_toolCombo->findData(static_cast<int>(MeasurementTool::EdgeRadius)),
		tr("STEP/IGES/BREP parts only - click directly on a circular edge (hole or boss). Not available for glTF/OBJ meshes."),
		Qt::ToolTipRole);
	// Same CAD-only pick as Edge Radius above (both circular-edge anchors).
	_toolCombo->setItemData(_toolCombo->findData(static_cast<int>(MeasurementTool::Concentricity)),
		tr("STEP/IGES/BREP parts only - click directly on two circular edges (holes or bosses) to compare their centers and axes. Not available for glTF/OBJ meshes."),
		Qt::ToolTipRole);
	// The other variable-pick-count tool alongside Pitch Circle above - same
	// note about it not auto-completing at a fixed click count.
	_toolCombo->setItemData(_toolCombo->findData(static_cast<int>(MeasurementTool::EdgeChain)),
		tr("Click every edge you want summed (2 or more, any order, don't need to connect end-to-end) - a running total that works for an open chain (e.g. a weld seam) or a closed perimeter alike. Press Enter or the Finish button once you've clicked them all."),
		Qt::ToolTipRole);

	_statusLabel = new QLabel(this);
	_statusLabel->setWordWrap(true);

	_finishButton = new QPushButton(tr("Finish"), this);
	_finishButton->setVisible(false);

	_resultsList = new MeasurementResultsList(this);
	// Ctrl/Shift multi-select, so several measurements can be reviewed or
	// deleted together (see ModelViewer::deleteSelectedMeasurements()'s
	// undo-macro batching).
	_resultsList->setSelectionMode(QAbstractItemView::ExtendedSelection);

	_deleteButton = new QPushButton(tr("Delete"), this);
	_deleteButton->setEnabled(false);

	QHBoxLayout* buttonRow = new QHBoxLayout();
	buttonRow->addStretch();
	buttonRow->addWidget(_deleteButton);

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->addWidget(new QLabel(tr("Tool:"), this));
	layout->addWidget(_toolCombo);
	layout->addWidget(_statusLabel);
	layout->addWidget(_finishButton);
	layout->addWidget(new QLabel(tr("Measurements:"), this));
	layout->addWidget(_resultsList);
	layout->addLayout(buttonRow);
	resize(320, 400);

	connect(_toolCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MeasurementDialog::onToolComboChanged);
	connect(_resultsList, &QListWidget::itemSelectionChanged, this, &MeasurementDialog::onResultsSelectionChanged);
	connect(_resultsList, &QListWidget::itemChanged, this, &MeasurementDialog::onResultItemChanged);
	connect(_deleteButton, &QPushButton::clicked, this, &MeasurementDialog::onDeleteClicked);
	connect(_finishButton, &QPushButton::clicked, this, &MeasurementDialog::onFinishClicked);

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
	onToolComboChanged(_toolCombo->currentIndex());

	if (QMdiArea* mdiArea = findMdiArea(_modelViewer))
		connect(mdiArea, &QMdiArea::subWindowActivated, this, &MeasurementDialog::onActiveSubWindowChanged);

	loadSettings();  // restores window geometry, if any was saved - after resize(320, 400) above so it can override that default
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
	const MeasurementTool tool = static_cast<MeasurementTool>(_toolCombo->itemData(index).toInt());
	if (ViewportWidget* viewport = _modelViewer->getViewportWidget())
		viewport->setMeasurementTool(tool);
}

void MeasurementDialog::onMeasurementProgressChanged(int picked, int required)
{
	ViewportWidget* viewport = _modelViewer->getViewportWidget();
	if (!viewport)
		return;
	const MeasurementTool tool = viewport->measurementTool();
	_statusLabel->setText(measurementToolPickPrompt(tool, picked));

	// The Finish button only makes sense for a variable-anchor-count tool
	// (currently just Pitch Circle) - every other tool already auto-
	// completes the instant `required` is reached, so there's never
	// anything left to "finish" for them.
	const bool variableLength = measurementToolHasVariableAnchorCount(tool);
	_finishButton->setVisible(variableLength);
	_finishButton->setEnabled(variableLength && picked >= required);
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
		_statusLabel->setText(tr("Cancelled - pick a tool to resume"));
	else
		setComboToolSilently(tool);
}

void MeasurementDialog::setComboToolSilently(MeasurementTool tool)
{
	const QSignalBlocker blocker(_toolCombo);
	_toolCombo->setCurrentIndex(_toolCombo->findData(static_cast<int>(tool)));
}

void MeasurementDialog::onMeasurementsChanged()
{
	refreshResultsList();
}

void MeasurementDialog::refreshResultsList()
{
	SceneGraph* sceneGraph = _modelViewer->sceneGraph();
	ViewportWidget* viewport = _modelViewer->getViewportWidget();
	if (!sceneGraph || !viewport)
		return;

	const QSet<QUuid> selectedIds = viewport->selectedMeasurementIds();

	_updatingSelectionFromViewport = true;
	_resultsList->clear();
	for (const Measurement& m : sceneGraph->measurements())
	{
		QListWidgetItem* item = new QListWidgetItem(viewport->measurementSummaryText(m), _resultsList);
		item->setData(Qt::UserRole, m.id);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(m.visible ? Qt::Checked : Qt::Unchecked);
		if (selectedIds.contains(m.id))
			item->setSelected(true);
	}
	_updatingSelectionFromViewport = false;

	_deleteButton->setEnabled(!selectedIds.isEmpty());
}

void MeasurementDialog::onResultsSelectionChanged()
{
	if (_updatingSelectionFromViewport)
		return;

	ViewportWidget* viewport = _modelViewer->getViewportWidget();
	if (!viewport)
		return;

	QSet<QUuid> ids;
	for (QListWidgetItem* item : _resultsList->selectedItems())
		ids.insert(item->data(Qt::UserRole).toUuid());

	_updatingSelectionFromViewport = true;
	viewport->setSelectedMeasurementIds(ids);
	_updatingSelectionFromViewport = false;

	_deleteButton->setEnabled(!ids.isEmpty());
}

void MeasurementDialog::onViewportSelectionChanged(const QSet<QUuid>& ids)
{
	if (_updatingSelectionFromViewport)
		return;

	_updatingSelectionFromViewport = true;
	_resultsList->clearSelection();
	QListWidgetItem* firstMatch = nullptr;
	for (int i = 0; i < _resultsList->count(); ++i)
	{
		QListWidgetItem* item = _resultsList->item(i);
		if (ids.contains(item->data(Qt::UserRole).toUuid()))
		{
			item->setSelected(true);
			if (!firstMatch)
				firstMatch = item;
		}
	}
	if (firstMatch)
		_resultsList->scrollToItem(firstMatch);
	_updatingSelectionFromViewport = false;

	_deleteButton->setEnabled(!ids.isEmpty());
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
	// doesn't touch the GL viewport at all, so nothing else repaints it.
	// Every other measurement state change happens to trigger a repaint as a
	// side effect of some ViewportWidget method call along the way (e.g.
	// setSelectedMeasurementIds()'s own update()); visibility has no such
	// path, so it's requested explicitly here.
	if (viewport)
		viewport->update();
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
