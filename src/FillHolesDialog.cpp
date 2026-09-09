#include "FillHolesDialog.h"
#include "ui_FillHolesDialog.h"

#include "MeshRepair.h"
#include "ModelViewer.h"
#include "ViewportWidget.h"
#include "SceneGraph.h"
#include "SceneMesh.h"
#include "SceneTreeWidget.h"
#include "MainWindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QHash>
#include <QListWidget>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QStringList>
#include <QVector>

#include <algorithm>

namespace
{
	bool listContainsUuid(QListWidget* list, const QUuid& uuid)
	{
		for (int i = 0; i < list->count(); ++i)
		{
			if (list->item(i)->data(Qt::UserRole).toUuid() == uuid)
				return true;
		}
		return false;
	}

	// Walks up the parent chain from a widget inside the MDI area to find the QMdiArea itself -
	// same helper as RepairMeshDialog.cpp/ShrinkWrapDialog.cpp, redeclared locally per that
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

	// Highest N found among direct top-level nodes named "Fill Holes NNN" (0 if none) - same
	// convention as RepairMeshDialog.cpp's highestExistingRepairIndex().
	int highestExistingFillIndex(SceneNode* root)
	{
		if (!root)
			return 0;
		static const QRegularExpression pattern(QStringLiteral("^Fill Holes (\\d+)$"));
		int highest = 0;
		for (SceneNode* child : root->children)
		{
			if (!child)
				continue;
			const QRegularExpressionMatch m = pattern.match(child->name);
			if (m.hasMatch())
				highest = std::max(highest, m.captured(1).toInt());
		}
		return highest;
	}

	// holesList row -> the DetectedHole it describes. Qt::UserRole holds the owning mesh's
	// QUuid, Qt::UserRole + 1 its loopId - see FillHolesDialog::refreshHolesList()'s doc
	// comment (header) for why two roles instead of packing both into one QVariant.
	constexpr int kHoleLoopIdRole = Qt::UserRole + 1;
}

FillHolesDialog::FillHolesDialog(ModelViewer* modelViewer, QWidget* parent)
	: QDialog(parent)
	, _modelViewer(modelViewer)
	, ui(std::make_unique<Ui::FillHolesDialog>())
{
	ui->setupUi(this);
	setAttribute(Qt::WA_DeleteOnClose);

	connect(ui->addSelectedButton, &QPushButton::clicked, this, &FillHolesDialog::addCurrentTreeSelection);
	connect(ui->removeSelectedButton, &QPushButton::clicked, this, &FillHolesDialog::onRemoveSelectedClicked);
	connect(ui->generateButton, &QPushButton::clicked, this, &FillHolesDialog::onGenerateClicked);
	connect(ui->meshList, &QListWidget::itemSelectionChanged, this, &FillHolesDialog::onListSelectionChanged);
	connect(ui->holesList, &QListWidget::itemSelectionChanged, this, &FillHolesDialog::onHolesListSelectionChanged);
	connect(ui->holesList, &QListWidget::itemChanged, this, &FillHolesDialog::onHolesListItemChanged);
	connect(ui->selectAllHolesButton, &QPushButton::clicked, this, &FillHolesDialog::onSelectAllHolesClicked);
	connect(ui->deselectAllHolesButton, &QPushButton::clicked, this, &FillHolesDialog::onDeselectAllHolesClicked);

	if (_modelViewer->sceneGraph())
		_nextFillIndex = highestExistingFillIndex(_modelViewer->sceneGraph()->root()) + 1;

	updateActionButtonsEnabled();
	loadSettings();

	// Hide/show this dialog as its OWN document's MDI subwindow loses/gains focus - mirrors
	// RepairMeshDialog/RtRenderDialog's identical mechanism. Without this, a dialog opened for
	// one document kept showing (and still reflecting) that document's stale state even while a
	// different one became the active tab.
	if (_modelViewer)
	{
		if (QMdiArea* mdiArea = findMdiArea(_modelViewer))
			connect(mdiArea, &QMdiArea::subWindowActivated, this, &FillHolesDialog::onActiveSubWindowChanged);
	}
}

FillHolesDialog::~FillHolesDialog()
{
	if (_modelViewer && _modelViewer->getViewportWidget())
		_modelViewer->getViewportWidget()->clearDetectedHoles();
}

void FillHolesDialog::onActiveSubWindowChanged(QMdiSubWindow* activeSubWindow)
{
	const bool isOwnDocumentActive = _modelViewer
		&& activeSubWindow
		&& activeSubWindow->widget() == static_cast<QWidget*>(_modelViewer);
	setVisible(isOwnDocumentActive);
}

void FillHolesDialog::addCurrentTreeSelection()
{
	SceneTreeWidget* tree = _modelViewer->getTreeModel();
	if (!tree || !tree->hasMeshSelection())
		return;

	ViewportWidget* viewport = _modelViewer->getViewportWidget();
	bool added = false;
	for (const QUuid& uuid : tree->selectedMeshUuids())
	{
		if (listContainsUuid(ui->meshList, uuid))
			continue;
		SceneMesh* mesh = viewport ? viewport->getMeshByUuid(uuid) : nullptr;
		if (!mesh)
			continue;

		QListWidgetItem* item = new QListWidgetItem(mesh->getName(), ui->meshList);
		item->setData(Qt::UserRole, uuid);
		added = true;
	}

	if (added)
		refreshHolesList();
	updateActionButtonsEnabled();
}

void FillHolesDialog::onRemoveSelectedClicked()
{
	qDeleteAll(ui->meshList->selectedItems());
	refreshHolesList();
	updateActionButtonsEnabled();
}

void FillHolesDialog::onListSelectionChanged()
{
	ui->removeSelectedButton->setEnabled(!ui->meshList->selectedItems().isEmpty());
}

void FillHolesDialog::refreshHolesList()
{
	ui->holesList->clear();

	ViewportWidget* viewport = _modelViewer->getViewportWidget();
	if (!viewport)
		return;

	std::vector<DetectedHole> allHoles;
	{
		// Bulk-populating holesList fires itemChanged once per row via QListWidgetItem's
		// checkable-flag default - block it here rather than let updateActionButtonsEnabled()
		// run redundantly per row (it's called once, explicitly, at the end instead).
		const QSignalBlocker blocker(ui->holesList);

		for (int i = 0; i < ui->meshList->count(); ++i)
		{
			const QUuid meshUuid = ui->meshList->item(i)->data(Qt::UserRole).toUuid();
			SceneMesh* mesh = viewport->getMeshByUuid(meshUuid);
			if (!mesh)
				continue;

			const std::vector<DetectedHole> holes = SceneMesh::detectHoles(mesh);
			for (const DetectedHole& hole : holes)
			{
				const QString label = tr("%1 - Hole #%2 (%3 edges)")
					.arg(mesh->getName()).arg(hole.loopId + 1).arg(hole.edgeCount);
				QListWidgetItem* item = new QListWidgetItem(label, ui->holesList);
				item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
				// Default-checked - the interactive review IS the safety mechanism (no
				// separate max-edge-count auto-filter), see this class's doc comment.
				item->setCheckState(Qt::Checked);
				item->setData(Qt::UserRole, hole.meshUuid);
				item->setData(kHoleLoopIdRole, hole.loopId);
			}
			allHoles.insert(allHoles.end(), holes.begin(), holes.end());
		}
	}

	viewport->setDetectedHoles(std::move(allHoles));
	viewport->clearHighlightedHole();
	viewport->update();

	updateActionButtonsEnabled();
}

void FillHolesDialog::onHolesListSelectionChanged()
{
	ViewportWidget* viewport = _modelViewer->getViewportWidget();
	if (!viewport)
		return;

	const QList<QListWidgetItem*> selected = ui->holesList->selectedItems();
	if (selected.isEmpty())
		viewport->clearHighlightedHole();
	else
		viewport->setHighlightedHole(selected.first()->data(Qt::UserRole).toUuid(),
		                              selected.first()->data(kHoleLoopIdRole).toInt());
	viewport->update();
}

void FillHolesDialog::onHolesListItemChanged(QListWidgetItem* /*item*/)
{
	updateActionButtonsEnabled();
}

void FillHolesDialog::onSelectAllHolesClicked()
{
	const QSignalBlocker blocker(ui->holesList);
	for (int i = 0; i < ui->holesList->count(); ++i)
		ui->holesList->item(i)->setCheckState(Qt::Checked);
	updateActionButtonsEnabled();
}

void FillHolesDialog::onDeselectAllHolesClicked()
{
	const QSignalBlocker blocker(ui->holesList);
	for (int i = 0; i < ui->holesList->count(); ++i)
		ui->holesList->item(i)->setCheckState(Qt::Unchecked);
	updateActionButtonsEnabled();
}

void FillHolesDialog::updateActionButtonsEnabled()
{
	const bool holesListNonEmpty = ui->holesList->count() > 0;
	ui->selectAllHolesButton->setEnabled(holesListNonEmpty);
	ui->deselectAllHolesButton->setEnabled(holesListNonEmpty);

	bool anyChecked = false;
	for (int i = 0; i < ui->holesList->count() && !anyChecked; ++i)
		anyChecked = ui->holesList->item(i)->checkState() == Qt::Checked;
	ui->generateButton->setEnabled(anyChecked);
}

void FillHolesDialog::onGenerateClicked()
{
	ViewportWidget* viewport = _modelViewer->getViewportWidget();
	SceneGraph* sceneGraph = _modelViewer->sceneGraph();
	if (!viewport || !sceneGraph)
		return;

	// Groups holesList's checked rows by owning mesh - fillHoles() takes one mesh's whole set
	// of loopIds to fill in a single call.
	QHash<QUuid, QSet<int>> checkedLoopIdsByMesh;
	for (int i = 0; i < ui->holesList->count(); ++i)
	{
		QListWidgetItem* item = ui->holesList->item(i);
		if (item->checkState() != Qt::Checked)
			continue;
		checkedLoopIdsByMesh[item->data(Qt::UserRole).toUuid()].insert(item->data(kHoleLoopIdRole).toInt());
	}

	if (checkedLoopIdsByMesh.isEmpty())
	{
		ui->statusLabel->setText(tr("Check at least one detected hole first."));
		return;
	}

	ui->statusLabel->setText(tr("Filling holes..."));
	ui->generateButton->setEnabled(false);
	MainWindow::showIndeterminateProgressBar();
	MainWindow::setCancelButtonEnabled(false); // no cancellation mid-call is possible
	QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

	// Same "Replace previous result" convention as RepairMeshDialog - undoably deletes the
	// previous batch's results (one real DeleteMeshCommand push) before running this one.
	if (ui->replacePreviousCheckBox->isChecked() && !_lastResultMeshUuids.isEmpty())
	{
		_modelViewer->replaceToolResults(_lastResultMeshUuids, tr("Replace Fill Holes Result"));
		_lastResultMeshUuids.clear();
	}

	const QSet<QUuid> originalSelection = _modelViewer->getSelectedUuids();

	viewport->makeCurrent();

	SceneNode* topParent = sceneGraph->root();
	QVector<FillHolesResult> results;
	QVector<QUuid> newResultUuids;
	int filledCount = 0, skippedCount = 0, failedCount = 0;
	int totalHolesFilled = 0, totalNonManifoldFixed = 0;
	int meshesWithSelfIntersectionsResolved = 0, meshesWithUnresolvedSelfIntersections = 0;

	for (auto it = checkedLoopIdsByMesh.constBegin(); it != checkedLoopIdsByMesh.constEnd(); ++it)
	{
		SceneMesh* mesh = viewport->getMeshByUuid(it.key());
		if (!mesh)
			continue;

		const QString fillName = QStringLiteral("Fill Holes %1").arg(_nextFillIndex, 3, 10, QChar('0'));
		const QString meshName = viewport->generateUniqueMeshName(fillName);

		MeshRepairReport report;
		SceneMesh* filled = SceneMesh::fillHoles(mesh, it.value(), meshName, &report);

		if (!report.succeeded)
		{
			++failedCount;
			continue;
		}
		if (!filled)
		{
			++skippedCount; // stale loopIds - nothing in the current mesh matched
			continue;
		}

		viewport->addToDisplay(filled);
		const QUuid filledUuid = filled->uuid();

		SceneNode* fillNode = new SceneNode();
		fillNode->nodeUuid = QUuid::createUuid();
		fillNode->name = fillName;

		const int position = topParent->children.size();
		sceneGraph->insertChildNode(topParent, fillNode, position);
		sceneGraph->restoreMeshUuid(fillNode, filledUuid, 0);

		results.append({ fillNode, topParent, position, filledUuid });
		newResultUuids.append(filledUuid);

		++filledCount;
		totalHolesFilled += static_cast<int>(it.value().size());
		totalNonManifoldFixed += static_cast<int>(report.nonManifoldVerticesFixed);
		if (report.hadSelfIntersections && report.selfIntersectionsResolved)
			++meshesWithSelfIntersectionsResolved;
		// Same exclusion RepairMeshDialog applies - see MeshRepairReport::
		// selfIntersectionLikelyFromNonManifoldFix's doc comment in MeshRepair.h.
		else if (report.hadSelfIntersections && !report.selfIntersectionsResolved
			&& !report.selfIntersectionLikelyFromNonManifoldFix)
			++meshesWithUnresolvedSelfIntersections;

		++_nextFillIndex;
	}

	viewport->doneCurrent();
	viewport->updateView();
	_modelViewer->updateDisplayList();

	// Pushed immediately, not deferred to dialog close - matches RepairMeshDialog/ShrinkWrapDialog.
	// Wrapped in a single undo-stack macro internally when more than one mesh was filled (see
	// ModelViewer::commitFillHoles()), same "one Generate click, one Ctrl+Z" convention.
	if (!results.isEmpty())
		_modelViewer->commitFillHoles(results, originalSelection);
	_lastResultMeshUuids = newResultUuids;

	MainWindow::hideProgressBar();
	updateActionButtonsEnabled();

	QString summary;
	if (filledCount > 0)
	{
		summary = tr("%1 of %2 mesh(es) had holes filled").arg(filledCount).arg(checkedLoopIdsByMesh.size());
		QStringList details;
		details << tr("%1 hole(s) filled").arg(totalHolesFilled);
		if (totalNonManifoldFixed > 0)
			details << tr("%1 non-manifold vertex(es) fixed along the way").arg(totalNonManifoldFixed);
		if (meshesWithSelfIntersectionsResolved > 0)
			details << tr("%1 mesh(es) had self-intersections removed").arg(meshesWithSelfIntersectionsResolved);
		if (meshesWithUnresolvedSelfIntersections > 0)
			details << tr("%1 mesh(es) still have unresolved self-intersections").arg(meshesWithUnresolvedSelfIntersections);
		summary += tr(" (%1).").arg(details.join(tr(", ")));
		if (skippedCount > 0)
			summary += tr(" %1 skipped (stale selection).").arg(skippedCount);
		if (failedCount > 0)
			summary += tr(" %1 could not be repaired.").arg(failedCount);
	}
	else if (failedCount == 0)
	{
		summary = tr("Nothing filled - the checked hole(s) no longer matched their mesh(es).");
	}
	else
	{
		summary = tr("No mesh could be filled (%1 failed).").arg(failedCount);
	}
	ui->statusLabel->setText(summary);
}

void FillHolesDialog::closeEvent(QCloseEvent* event)
{
	saveSettings();
	if (_modelViewer && _modelViewer->getViewportWidget())
		_modelViewer->getViewportWidget()->clearDetectedHoles();
	QDialog::closeEvent(event);
}

void FillHolesDialog::reject()
{
	// Escape reaches here, not closeEvent() (see this override's doc comment in the header) -
	// closeEvent() only does saveSettings()/overlay teardown now, so this just needs to make
	// sure Escape doesn't skip it too.
	saveSettings();
	if (_modelViewer && _modelViewer->getViewportWidget())
		_modelViewer->getViewportWidget()->clearDetectedHoles();
	QDialog::reject();
}

void FillHolesDialog::loadSettings()
{
	QSettings settings;
	const QByteArray geometry = settings.value("fillHoles/geometry", QByteArray()).toByteArray();
	if (!geometry.isEmpty())
		restoreGeometry(geometry);
}

void FillHolesDialog::saveSettings()
{
	QSettings settings;
	settings.setValue("fillHoles/geometry", saveGeometry());
}
