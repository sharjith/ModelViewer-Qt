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
#include <QPushButton>
#include <QSignalBlocker>
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
}

MeasurementDialog::MeasurementDialog(ModelViewer* modelViewer, QWidget* parent)
	: QDialog(parent)
	, _modelViewer(modelViewer)
{
	setWindowTitle(tr("Measure"));
	setAttribute(Qt::WA_DeleteOnClose);

	_toolCombo = new QComboBox(this);
	for (MeasurementTool tool : { MeasurementTool::Point, MeasurementTool::Distance,
	                               MeasurementTool::ArcRadius3Point, MeasurementTool::ArcRadiusCenterPoint })
	{
		_toolCombo->addItem(measurementToolDisplayName(tool), static_cast<int>(tool));
	}
	// Boss-only limitation (see MeasurementData.h's ArcRadiusCenterPoint doc
	// comment) - surfaced here so it's discoverable without reading code: a
	// through-hole's center has no geometry to click, only a raised boss's
	// flat cap face does.
	_toolCombo->setItemData(_toolCombo->findData(static_cast<int>(MeasurementTool::ArcRadiusCenterPoint)),
		tr("Center must land on real geometry (e.g. a boss's flat cap face) - won't work for a through-hole's center, which is empty space"),
		Qt::ToolTipRole);

	_statusLabel = new QLabel(this);
	_statusLabel->setWordWrap(true);

	_resultsList = new QListWidget(this);
	_resultsList->setSelectionMode(QAbstractItemView::SingleSelection);

	_deleteButton = new QPushButton(tr("Delete"), this);
	_deleteButton->setEnabled(false);

	QHBoxLayout* buttonRow = new QHBoxLayout();
	buttonRow->addStretch();
	buttonRow->addWidget(_deleteButton);

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->addWidget(new QLabel(tr("Tool:"), this));
	layout->addWidget(_toolCombo);
	layout->addWidget(_statusLabel);
	layout->addWidget(new QLabel(tr("Measurements:"), this));
	layout->addWidget(_resultsList);
	layout->addLayout(buttonRow);
	resize(320, 400);

	connect(_toolCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MeasurementDialog::onToolComboChanged);
	connect(_resultsList, &QListWidget::itemSelectionChanged, this, &MeasurementDialog::onResultsSelectionChanged);
	connect(_resultsList, &QListWidget::itemChanged, this, &MeasurementDialog::onResultItemChanged);
	connect(_deleteButton, &QPushButton::clicked, this, &MeasurementDialog::onDeleteClicked);

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
	QDialog::closeEvent(event);
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
	if (ViewportWidget* viewport = _modelViewer->getViewportWidget())
		_statusLabel->setText(measurementToolPickPrompt(viewport->measurementTool(), picked));
	Q_UNUSED(required);
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

	const QUuid selectedId = viewport->selectedMeasurementId();

	_updatingSelectionFromViewport = true;
	_resultsList->clear();
	for (const Measurement& m : sceneGraph->measurements())
	{
		QListWidgetItem* item = new QListWidgetItem(viewport->measurementSummaryText(m), _resultsList);
		item->setData(Qt::UserRole, m.id);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(m.visible ? Qt::Checked : Qt::Unchecked);
		if (m.id == selectedId)
			item->setSelected(true);
	}
	_updatingSelectionFromViewport = false;

	_deleteButton->setEnabled(!selectedId.isNull());
}

void MeasurementDialog::onResultsSelectionChanged()
{
	if (_updatingSelectionFromViewport)
		return;

	ViewportWidget* viewport = _modelViewer->getViewportWidget();
	if (!viewport)
		return;

	const QList<QListWidgetItem*> selected = _resultsList->selectedItems();
	const QUuid id = selected.isEmpty() ? QUuid() : selected.first()->data(Qt::UserRole).toUuid();

	_updatingSelectionFromViewport = true;
	viewport->setSelectedMeasurementId(id);
	_updatingSelectionFromViewport = false;

	_deleteButton->setEnabled(!id.isNull());
}

void MeasurementDialog::onViewportSelectionChanged(const QUuid& id)
{
	if (_updatingSelectionFromViewport)
		return;

	_updatingSelectionFromViewport = true;
	_resultsList->clearSelection();
	if (!id.isNull())
	{
		for (int i = 0; i < _resultsList->count(); ++i)
		{
			QListWidgetItem* item = _resultsList->item(i);
			if (item->data(Qt::UserRole).toUuid() == id)
			{
				item->setSelected(true);
				_resultsList->scrollToItem(item);
				break;
			}
		}
	}
	_updatingSelectionFromViewport = false;

	_deleteButton->setEnabled(!id.isNull());
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
	// setSelectedMeasurementId()'s own update()); visibility has no such
	// path, so it's requested explicitly here.
	if (viewport)
		viewport->update();
}

void MeasurementDialog::onDeleteClicked()
{
	if (_modelViewer)
		_modelViewer->deleteSelectedMeasurement();
}

void MeasurementDialog::onActiveSubWindowChanged(QMdiSubWindow* activeSubWindow)
{
	const bool isOwnDocumentActive = _modelViewer
		&& activeSubWindow
		&& activeSubWindow->widget() == static_cast<QWidget*>(_modelViewer);
	setVisible(isOwnDocumentActive);
}
