#include "ShrinkWrapDialog.h"
#include "ui_ShrinkWrapDialog.h"

#include "ModelViewer.h"
#include "ViewportWidget.h"
#include "SceneGraph.h"
#include "SceneMesh.h"
#include "SceneTreeWidget.h"
#include "MainWindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QListWidget>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
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

	// Highest N found among direct top-level nodes named "Shrink Wrap NNN"
	// (0 if none) - lets a fresh dialog/document keep numbering forward
	// instead of restarting at 1 and colliding with an already-committed name.
	int highestExistingWrapIndex(SceneNode* root)
	{
		if (!root)
			return 0;
		static const QRegularExpression pattern(QStringLiteral("^Shrink Wrap (\\d+)$"));
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

ShrinkWrapDialog::ShrinkWrapDialog(ModelViewer* modelViewer, QWidget* parent)
	: QDialog(parent)
	, _modelViewer(modelViewer)
	, ui(std::make_unique<Ui::ShrinkWrapDialog>())
{
	ui->setupUi(this);
	setAttribute(Qt::WA_DeleteOnClose);

	connect(ui->addSelectedButton, &QPushButton::clicked, this, &ShrinkWrapDialog::addCurrentTreeSelection);
	connect(ui->removeSelectedButton, &QPushButton::clicked, this, &ShrinkWrapDialog::onRemoveSelectedClicked);
	connect(ui->resetToleranceButton, &QPushButton::clicked, this, &ShrinkWrapDialog::onResetToleranceClicked);
	connect(ui->generateButton, &QPushButton::clicked, this, &ShrinkWrapDialog::onGenerateClicked);
	connect(ui->meshList, &QListWidget::itemSelectionChanged, this, &ShrinkWrapDialog::onListSelectionChanged);

	if (_modelViewer->sceneGraph())
		_nextWrapIndex = highestExistingWrapIndex(_modelViewer->sceneGraph()->root()) + 1;

	updateActionButtonsEnabled();
	loadSettings();
}

ShrinkWrapDialog::~ShrinkWrapDialog()
{
}

void ShrinkWrapDialog::addCurrentTreeSelection()
{
	SceneTreeWidget* tree = _modelViewer->getTreeModel();
	if (!tree || !tree->hasMeshSelection())
		return;

	const bool wasEmpty = (ui->meshList->count() == 0);

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

	if (wasEmpty && ui->meshList->count() > 0)
		refreshSuggestedTolerance();
	updateActionButtonsEnabled();
}

void ShrinkWrapDialog::onRemoveSelectedClicked()
{
	qDeleteAll(ui->meshList->selectedItems());
	updateActionButtonsEnabled();
}

void ShrinkWrapDialog::onListSelectionChanged()
{
	ui->removeSelectedButton->setEnabled(!ui->meshList->selectedItems().isEmpty());
}

void ShrinkWrapDialog::updateActionButtonsEnabled()
{
	const bool hasMeshes = ui->meshList->count() > 0;
	ui->generateButton->setEnabled(hasMeshes);
	ui->resetToleranceButton->setEnabled(hasMeshes);
}

void ShrinkWrapDialog::onResetToleranceClicked()
{
	refreshSuggestedTolerance();
}

void ShrinkWrapDialog::refreshSuggestedTolerance()
{
	ViewportWidget* viewport = _modelViewer->getViewportWidget();
	if (!viewport)
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

	double alpha = 0.0, offset = 0.0;
	SceneMesh::suggestShrinkWrapTolerance(meshes, alpha, offset);
	if (alpha > 0.0)
		ui->alphaSpin->setValue(alpha);
	if (offset > 0.0)
		ui->offsetSpin->setValue(offset);
}

void ShrinkWrapDialog::discardAllPreviews()
{
	if (_previews.isEmpty())
		return;

	ViewportWidget* viewport = _modelViewer->getViewportWidget();
	SceneGraph* sceneGraph = _modelViewer->sceneGraph();
	if (viewport && sceneGraph)
	{
		for (const PreviewEntry& entry : _previews)
		{
			const int meshIndex = viewport->getIndexByUuid(entry.meshUuid);
			if (meshIndex >= 0)
				viewport->moveToRecycleBin(entry.meshUuid, meshIndex);

			int pos = 0;
			sceneGraph->removeMeshUuid(entry.meshUuid, pos);

			int outPosition = 0;
			sceneGraph->removeChildNode(entry.parent, entry.node, outPosition);

			viewport->permanentlyDeleteFromBin(entry.meshUuid);
			SceneGraph::deleteDetachedSubtree(entry.node);
		}
	}
	_previews.clear();
}

void ShrinkWrapDialog::onGenerateClicked()
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

	// Replace the stale message from the previous run immediately, and
	// force it to actually paint before the (synchronous, potentially
	// slow) alpha_wrap_3 call below blocks the event loop. alpha_wrap_3
	// reports no percentage of its own, so this is the indeterminate/busy
	// style, not a determinate one (see MainWindow::showIndeterminateProgressBar()).
	ui->statusLabel->setText(tr("Generating..."));
	ui->generateButton->setEnabled(false);
	MainWindow::showIndeterminateProgressBar();
	MainWindow::setCancelButtonEnabled(false); // no cancellation mid-call is possible
	QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

	if (ui->replacePreviousCheckBox->isChecked())
		discardAllPreviews();

	viewport->makeCurrent();

	const QString wrapName = QStringLiteral("Shrink Wrap %1").arg(_nextWrapIndex, 3, 10, QChar('0'));
	const QString meshName = viewport->generateUniqueMeshName(wrapName);
	SceneMesh* wrapped = SceneMesh::shrinkWrapMeshes(meshes, meshName, ui->alphaSpin->value(), ui->offsetSpin->value());
	if (!wrapped)
	{
		viewport->doneCurrent();
		MainWindow::hideProgressBar();
		ui->generateButton->setEnabled(true);
		ui->statusLabel->setText(tr("Shrink Wrap failed - no geometry was produced."));
		return;
	}
	viewport->addToDisplay(wrapped);
	const QUuid wrappedUuid = wrapped->uuid();

	SceneNode* wrapNode = new SceneNode();
	wrapNode->nodeUuid = QUuid::createUuid();
	wrapNode->name = wrapName;

	SceneNode* topParent = sceneGraph->root();
	const int wrapPosition = topParent->children.size();
	sceneGraph->insertChildNode(topParent, wrapNode, wrapPosition);
	sceneGraph->restoreMeshUuid(wrapNode, wrappedUuid, 0);

	viewport->doneCurrent();
	viewport->updateView();
	_modelViewer->updateDisplayList();

	PreviewEntry entry;
	entry.node = wrapNode;
	entry.parent = topParent;
	entry.position = wrapPosition;
	entry.meshUuid = wrappedUuid;
	_previews.append(entry);
	++_nextWrapIndex;

	MainWindow::hideProgressBar();
	ui->generateButton->setEnabled(true);
	ui->statusLabel->setText(tr("%1: %2 mesh(es), %3 vertices, %4 triangles.")
	                              .arg(wrapName)
	                              .arg(meshes.size())
	                              .arg(wrapped->vertices().size())
	                              .arg(wrapped->indices().size() / 3));
}

void ShrinkWrapDialog::closeEvent(QCloseEvent* event)
{
	commitLivePreviews();
	saveSettings();
	QDialog::closeEvent(event);
}

void ShrinkWrapDialog::reject()
{
	// Escape reaches here, not closeEvent() (see this override's doc comment
	// in the header) - same commit, so Escape and every other close path
	// leave the scene/undo-stack in the same state.
	commitLivePreviews();
	saveSettings();
	QDialog::reject();
}

void ShrinkWrapDialog::commitLivePreviews()
{
	if (_previews.isEmpty())
		return;

	const QSet<QUuid> originalSelection = _modelViewer->getSelectedUuids();
	for (const PreviewEntry& entry : _previews)
	{
		_modelViewer->commitShrinkWrap(entry.node, entry.parent, entry.position, entry.meshUuid,
		                                originalSelection);
	}
	_previews.clear();
}

void ShrinkWrapDialog::loadSettings()
{
	QSettings settings;
	const QByteArray geometry = settings.value("shrinkWrap/geometry", QByteArray()).toByteArray();
	if (!geometry.isEmpty())
		restoreGeometry(geometry);
}

void ShrinkWrapDialog::saveSettings()
{
	QSettings settings;
	settings.setValue("shrinkWrap/geometry", saveGeometry());
}
