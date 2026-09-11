#include "RepairMeshDialog.h"
#include "ui_RepairMeshDialog.h"

#include "MeshRepair.h"
#include "ModelViewer.h"
#include "ViewportWidget.h"
#include "SceneGraph.h"
#include "SceneMesh.h"
#include "SceneTreeWidget.h"
#include "MainWindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QListWidget>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
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
	// same helper as ShrinkWrapDialog.cpp/RtRenderDialog.cpp, redeclared locally per that
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

	// Highest N found among direct top-level nodes named "Repair Mesh NNN" (0 if none) - same
	// convention as ShrinkWrapDialog.cpp's highestExistingWrapIndex().
	int highestExistingRepairIndex(SceneNode* root)
	{
		if (!root)
			return 0;
		static const QRegularExpression pattern(QStringLiteral("^Repair Mesh (\\d+)$"));
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
}

RepairMeshDialog::RepairMeshDialog(ModelViewer* modelViewer, QWidget* parent)
	: QDialog(parent)
	, _modelViewer(modelViewer)
	, ui(std::make_unique<Ui::RepairMeshDialog>())
{
	ui->setupUi(this);
	setAttribute(Qt::WA_DeleteOnClose);

	connect(ui->addSelectedButton, &QPushButton::clicked, this, &RepairMeshDialog::addCurrentTreeSelection);
	connect(ui->removeSelectedButton, &QPushButton::clicked, this, &RepairMeshDialog::onRemoveSelectedClicked);
	connect(ui->generateButton, &QPushButton::clicked, this, &RepairMeshDialog::onGenerateClicked);
	connect(ui->meshList, &QListWidget::itemSelectionChanged, this, &RepairMeshDialog::onListSelectionChanged);

	if (_modelViewer->sceneGraph())
		_nextRepairIndex = highestExistingRepairIndex(_modelViewer->sceneGraph()->root()) + 1;

	updateActionButtonsEnabled();
	loadSettings();

	// Hide/show this dialog as its OWN document's MDI subwindow loses/gains focus - mirrors
	// RtRenderDialog's identical mechanism. Without this, a dialog opened for one document kept
	// showing (and still reflecting) that document's stale state even while a different one
	// became the active tab.
	if (_modelViewer)
	{
		if (QMdiArea* mdiArea = findMdiArea(_modelViewer))
			connect(mdiArea, &QMdiArea::subWindowActivated, this, &RepairMeshDialog::onActiveSubWindowChanged);
	}
}

RepairMeshDialog::~RepairMeshDialog()
{
}

void RepairMeshDialog::onActiveSubWindowChanged(QMdiSubWindow* activeSubWindow)
{
	const bool isOwnDocumentActive = _modelViewer
		&& activeSubWindow
		&& activeSubWindow->widget() == static_cast<QWidget*>(_modelViewer);
	setVisible(isOwnDocumentActive);
}

void RepairMeshDialog::addCurrentTreeSelection()
{
	SceneTreeWidget* tree = _modelViewer->getTreeModel();
	if (!tree || !tree->hasMeshSelection())
		return;

	ViewportWidget* viewport = _modelViewer->getViewportWidget();
	for (const QUuid& uuid : tree->selectedMeshUuids())
	{
		if (listContainsUuid(ui->meshList, uuid))
			continue;
		SceneMesh* mesh = viewport ? viewport->getMeshByUuid(uuid) : nullptr;
		if (!mesh)
			continue;

		QListWidgetItem* item = new QListWidgetItem(mesh->getName(), ui->meshList);
		item->setData(Qt::UserRole, uuid);
	}

	updateActionButtonsEnabled();
}

void RepairMeshDialog::onRemoveSelectedClicked()
{
	qDeleteAll(ui->meshList->selectedItems());
	updateActionButtonsEnabled();
}

void RepairMeshDialog::onListSelectionChanged()
{
	ui->removeSelectedButton->setEnabled(!ui->meshList->selectedItems().isEmpty());
}

void RepairMeshDialog::updateActionButtonsEnabled()
{
	ui->generateButton->setEnabled(ui->meshList->count() > 0);
}

void RepairMeshDialog::onGenerateClicked()
{
	ViewportWidget* viewport = _modelViewer->getViewportWidget();
	SceneGraph* sceneGraph = _modelViewer->sceneGraph();
	if (!viewport || !sceneGraph)
		return;

	QVector<SceneMesh*> meshes;
	for (int i = 0; i < ui->meshList->count(); ++i)
	{
		SceneMesh* mesh = viewport->getMeshByUuid(ui->meshList->item(i)->data(Qt::UserRole).toUuid());
		if (mesh)
			meshes.append(mesh);
	}

	if (meshes.isEmpty())
	{
		ui->statusLabel->setText(tr("Add at least one mesh to the list first."));
		return;
	}

	ui->statusLabel->setText(tr("Repairing..."));
	ui->generateButton->setEnabled(false);
	MainWindow::showIndeterminateProgressBar();
	MainWindow::setCancelButtonEnabled(false); // no cancellation mid-call is possible
	QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

	// Same "Replace previous result" convention as ShrinkWrapDialog - undoably deletes the
	// previous batch's results (one real DeleteMeshCommand push) before running this one.
	if (ui->replacePreviousCheckBox->isChecked() && !_lastResultMeshUuids.isEmpty())
	{
		_modelViewer->replaceToolResults(_lastResultMeshUuids, tr("Replace Repair Mesh Result"));
		_lastResultMeshUuids.clear();
	}

	const QSet<QUuid> originalSelection = _modelViewer->getSelectedUuids();

	const int maxSelfIntersectionSteps = ui->selfIntersectionStepsSpin->value();
	const bool trySmoothingForSelfIntersections = ui->trySmoothingCheckBox->isChecked();

	viewport->makeCurrent();

	SceneNode* topParent = sceneGraph->root();
	QVector<RepairMeshResult> results;
	QVector<QUuid> newResultUuids;
	int repairedCount = 0, alreadyValidCount = 0, failedCount = 0;
	int totalNonManifoldFixed = 0, meshesWithSelfIntersectionsResolved = 0, meshesWithUnresolvedSelfIntersections = 0;

	for (SceneMesh* mesh : meshes)
	{
		const QString repairName = QStringLiteral("Repair Mesh %1").arg(_nextRepairIndex, 3, 10, QChar('0'));
		const QString meshName = viewport->generateUniqueMeshName(repairName);

		MeshRepairReport report;
		SceneMesh* repaired = SceneMesh::repairMesh(mesh, meshName, &report,
		                                             maxSelfIntersectionSteps, trySmoothingForSelfIntersections);

		if (!report.succeeded)
		{
			++failedCount;
			continue;
		}
		if (report.wasAlreadyValid || !repaired)
		{
			++alreadyValidCount;
			continue;
		}

		viewport->addToDisplay(repaired);
		const QUuid repairedUuid = repaired->uuid();

		SceneNode* repairNode = new SceneNode();
		repairNode->nodeUuid = QUuid::createUuid();
		repairNode->name = repairName;

		const int position = topParent->children.size();
		sceneGraph->insertChildNode(topParent, repairNode, position);
		sceneGraph->restoreMeshUuid(repairNode, repairedUuid, 0);

		results.append({ repairNode, topParent, position, repairedUuid });
		newResultUuids.append(repairedUuid);

		++repairedCount;
		totalNonManifoldFixed += static_cast<int>(report.nonManifoldVerticesFixed);
		if (report.hadSelfIntersections && report.selfIntersectionsResolved)
			++meshesWithSelfIntersectionsResolved;
		// Deliberately excludes report.selfIntersectionLikelyFromNonManifoldFix - that case is the
		// harmless coincident-point byproduct of the non-manifold-vertex fix counted above, not a
		// real unresolved defect worth flagging (see its doc comment in MeshRepair.h).
		else if (report.hadSelfIntersections && !report.selfIntersectionsResolved
			&& !report.selfIntersectionLikelyFromNonManifoldFix)
			++meshesWithUnresolvedSelfIntersections;

		++_nextRepairIndex;
	}

	viewport->doneCurrent();
	viewport->updateView();
	_modelViewer->updateDisplayList();

	// Pushed immediately, not deferred to dialog close - matches ShrinkWrapDialog. Wrapped in a
	// single undo-stack macro internally when more than one mesh was repaired (see
	// ModelViewer::commitRepairMesh()), same "one Generate click, one Ctrl+Z" convention as
	// commitUVGeneration()'s multi-mesh batching.
	if (!results.isEmpty())
		_modelViewer->commitRepairMesh(results, originalSelection);
	_lastResultMeshUuids = newResultUuids;

	MainWindow::hideProgressBar();
	ui->generateButton->setEnabled(true);

	QString summary;
	if (repairedCount > 0)
	{
		summary = tr("%1 of %2 mesh(es) repaired").arg(repairedCount).arg(meshes.size());
		QStringList details;
		if (totalNonManifoldFixed > 0)
			details << tr("%1 non-manifold vertex(es) fixed").arg(totalNonManifoldFixed);
		if (meshesWithSelfIntersectionsResolved > 0)
			details << tr("%1 mesh(es) had self-intersections removed").arg(meshesWithSelfIntersectionsResolved);
		if (meshesWithUnresolvedSelfIntersections > 0)
			details << tr("%1 mesh(es) still have unresolved self-intersections").arg(meshesWithUnresolvedSelfIntersections);
		if (!details.isEmpty())
			summary += tr(" (%1)").arg(details.join(tr(", ")));
		summary += tr(".");
		if (alreadyValidCount > 0)
			summary += tr(" %1 already valid.").arg(alreadyValidCount);
		if (failedCount > 0)
			summary += tr(" %1 could not be repaired.").arg(failedCount);
	}
	else if (failedCount == 0)
	{
		summary = tr("All %1 mesh(es) already valid - nothing to repair.").arg(meshes.size());
	}
	else
	{
		summary = tr("No mesh could be repaired (%1 failed).").arg(failedCount);
	}
	ui->statusLabel->setText(summary);
}

void RepairMeshDialog::closeEvent(QCloseEvent* event)
{
	saveSettings();
	QDialog::closeEvent(event);
}

void RepairMeshDialog::reject()
{
	// Escape reaches here, not closeEvent() (see this override's doc comment in the header) -
	// closeEvent() only does saveSettings() now, so this just needs to make sure Escape doesn't
	// skip it too.
	saveSettings();
	QDialog::reject();
}

void RepairMeshDialog::loadSettings()
{
	QSettings settings;
	const QByteArray geometry = settings.value("repairMesh/geometry", QByteArray()).toByteArray();
	if (!geometry.isEmpty())
		restoreGeometry(geometry);
}

void RepairMeshDialog::saveSettings()
{
	QSettings settings;
	settings.setValue("repairMesh/geometry", saveGeometry());
}
