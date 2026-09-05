#include "SubdivisionDialog.h"
#include "ui_SubdivisionDialog.h"

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
#include <QSet>
#include <QSettings>
#include <QVector>

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
	// same helper as RtRenderDialog.cpp, redeclared locally per that file's own convention.
	QMdiArea* findMdiArea(QWidget* widget)
	{
		for (QWidget* w = widget; w; w = w->parentWidget())
		{
			if (auto* area = qobject_cast<QMdiArea*>(w))
				return area;
		}
		return nullptr;
	}

	// Combo box row order matches SceneMesh::SubdivisionMethod's declaration
	// order (Loop = 0, CatmullClark = 1) - see ui/SubdivisionDialog.ui.
	SceneMesh::SubdivisionMethod methodFromComboIndex(int index)
	{
		return (index == 1) ? SceneMesh::SubdivisionMethod::CatmullClark
		                     : SceneMesh::SubdivisionMethod::Loop;
	}
}

SubdivisionDialog::SubdivisionDialog(ModelViewer* modelViewer, QWidget* parent)
	: QDialog(parent)
	, _modelViewer(modelViewer)
	, ui(std::make_unique<Ui::SubdivisionDialog>())
{
	ui->setupUi(this);
	setAttribute(Qt::WA_DeleteOnClose);

	connect(ui->addSelectedButton, &QPushButton::clicked, this, &SubdivisionDialog::addCurrentTreeSelection);
	connect(ui->removeSelectedButton, &QPushButton::clicked, this, &SubdivisionDialog::onRemoveSelectedClicked);
	connect(ui->generateButton, &QPushButton::clicked, this, &SubdivisionDialog::onGenerateClicked);
	connect(ui->meshList, &QListWidget::itemSelectionChanged, this, &SubdivisionDialog::onListSelectionChanged);

	updateActionButtonsEnabled();
	loadSettings();

	// Hide/show this dialog as its OWN document's MDI subwindow loses/gains focus - mirrors
	// RtRenderDialog's identical mechanism. Without this, a dialog opened for one document kept
	// showing (and still reflecting) that document's stale state even while a different one
	// became the active tab.
	if (_modelViewer)
	{
		if (QMdiArea* mdiArea = findMdiArea(_modelViewer))
			connect(mdiArea, &QMdiArea::subWindowActivated, this, &SubdivisionDialog::onActiveSubWindowChanged);
	}
}

SubdivisionDialog::~SubdivisionDialog()
{
}

void SubdivisionDialog::onActiveSubWindowChanged(QMdiSubWindow* activeSubWindow)
{
	const bool isOwnDocumentActive = _modelViewer
		&& activeSubWindow
		&& activeSubWindow->widget() == static_cast<QWidget*>(_modelViewer);
	setVisible(isOwnDocumentActive);
}

void SubdivisionDialog::addCurrentTreeSelection()
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

void SubdivisionDialog::onRemoveSelectedClicked()
{
	qDeleteAll(ui->meshList->selectedItems());
	updateActionButtonsEnabled();
}

void SubdivisionDialog::onListSelectionChanged()
{
	ui->removeSelectedButton->setEnabled(!ui->meshList->selectedItems().isEmpty());
}

void SubdivisionDialog::updateActionButtonsEnabled()
{
	ui->generateButton->setEnabled(ui->meshList->count() > 0);
}

void SubdivisionDialog::onGenerateClicked()
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

	ui->statusLabel->setText(tr("Generating..."));
	ui->generateButton->setEnabled(false);
	MainWindow::showIndeterminateProgressBar();
	MainWindow::setCancelButtonEnabled(false); // no cancellation mid-call is possible
	QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

	// "Replace previous result" now undoably deletes the last batch (one
	// real DeleteMeshCommand push) instead of discarding scratch state
	// outside the undo stack - see ModelViewer::replaceToolResults()'s doc
	// comment. Same before-the-loop ordering the old discardAllPreviews()
	// call used.
	if (ui->replacePreviousCheckBox->isChecked() && !_lastResultMeshUuids.isEmpty())
	{
		_modelViewer->replaceToolResults(_lastResultMeshUuids, tr("Replace Subdivision Result"));
		_lastResultMeshUuids.clear();
	}

	// Captured once for this whole click, not per result - every result
	// this Generate produces was made against the same "before" selection.
	const QSet<QUuid> originalSelection = _modelViewer->getSelectedUuids();

	viewport->makeCurrent();

	// Subdivision is per-mesh, topology-preserving refinement, not a
	// combine (unlike Shrink Wrap) - each list entry gets its own
	// independent result, so this loops over the whole list rather than
	// making one combined call.
	const SceneMesh::SubdivisionMethod method = methodFromComboIndex(ui->methodCombo->currentIndex());
	const unsigned int iterations = static_cast<unsigned int>(ui->iterationsSpin->value());
	const bool preserveSharpFeatures = ui->preserveSharpFeaturesCheckBox->isChecked();

	SceneNode* topParent = sceneGraph->root();
	int succeeded = 0;
	int failed = 0;
	qsizetype totalVertices = 0;
	qsizetype totalTriangles = 0;
	QVector<QUuid> newResultMeshUuids;

	for (SceneMesh* mesh : meshes)
	{
		const QString resultName = viewport->generateUniqueMeshName(mesh->getName() + "_Subdivided");
		SceneMesh* subdivided = SceneMesh::subdivideMesh(mesh, method, iterations, resultName,
		                                                 preserveSharpFeatures);
		if (!subdivided)
		{
			++failed;
			continue;
		}
		viewport->addToDisplay(subdivided);
		const QUuid resultUuid = subdivided->uuid();

		SceneNode* resultNode = new SceneNode();
		resultNode->nodeUuid = QUuid::createUuid();
		resultNode->name = resultName;

		const int resultPosition = topParent->children.size();
		sceneGraph->insertChildNode(topParent, resultNode, resultPosition);
		sceneGraph->restoreMeshUuid(resultNode, resultUuid, 0);

		// Pushed immediately, not deferred to dialog close - matches
		// MeasurementDialog: every result is independently undoable right
		// away (see this class's header doc comment for the bug this fixes).
		_modelViewer->commitSubdivision(resultNode, topParent, resultPosition, resultUuid, originalSelection);
		newResultMeshUuids.append(resultUuid);

		++succeeded;
		totalVertices += subdivided->vertices().size();
		totalTriangles += subdivided->indices().size() / 3;
	}

	_lastResultMeshUuids = newResultMeshUuids;

	viewport->doneCurrent();
	viewport->updateView();
	_modelViewer->updateDisplayList();

	MainWindow::hideProgressBar();
	ui->generateButton->setEnabled(true);

	if (succeeded == 0)
	{
		ui->statusLabel->setText(tr("Subdivision failed for all %1 mesh(es) - no geometry was produced.").arg(meshes.size()));
		return;
	}

	if (failed > 0)
	{
		ui->statusLabel->setText(tr("%1 of %2 mesh(es) subdivided (%3 failed): %4 vertices, %5 triangles total.")
		                              .arg(succeeded)
		                              .arg(meshes.size())
		                              .arg(failed)
		                              .arg(totalVertices)
		                              .arg(totalTriangles));
	}
	else
	{
		ui->statusLabel->setText(tr("%1 mesh(es) subdivided: %2 vertices, %3 triangles total.")
		                              .arg(succeeded)
		                              .arg(totalVertices)
		                              .arg(totalTriangles));
	}
}

void SubdivisionDialog::closeEvent(QCloseEvent* event)
{
	saveSettings();
	QDialog::closeEvent(event);
}

void SubdivisionDialog::reject()
{
	// Escape reaches here, not closeEvent() (see this override's doc comment
	// in the header) - closeEvent() only does saveSettings() now, so this
	// just needs to make sure Escape doesn't skip it too.
	saveSettings();
	QDialog::reject();
}

void SubdivisionDialog::loadSettings()
{
	QSettings settings;
	const QByteArray geometry = settings.value("subdivision/geometry", QByteArray()).toByteArray();
	if (!geometry.isEmpty())
		restoreGeometry(geometry);
}

void SubdivisionDialog::saveSettings()
{
	QSettings settings;
	settings.setValue("subdivision/geometry", saveGeometry());
}
