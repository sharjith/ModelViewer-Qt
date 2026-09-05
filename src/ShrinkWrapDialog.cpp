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
#include <QMdiArea>
#include <QMdiSubWindow>
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

	// Hide/show this dialog as its OWN document's MDI subwindow loses/gains focus - mirrors
	// RtRenderDialog's identical mechanism. Without this, a dialog opened for one document kept
	// showing (and still reflecting) that document's stale state even while a different one
	// became the active tab.
	if (_modelViewer)
	{
		if (QMdiArea* mdiArea = findMdiArea(_modelViewer))
			connect(mdiArea, &QMdiArea::subWindowActivated, this, &ShrinkWrapDialog::onActiveSubWindowChanged);
	}
}

ShrinkWrapDialog::~ShrinkWrapDialog()
{
}

void ShrinkWrapDialog::onActiveSubWindowChanged(QMdiSubWindow* activeSubWindow)
{
	const bool isOwnDocumentActive = _modelViewer
		&& activeSubWindow
		&& activeSubWindow->widget() == static_cast<QWidget*>(_modelViewer);
	setVisible(isOwnDocumentActive);
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

	// "Replace previous result" now undoably deletes the last result (one
	// real DeleteMeshCommand push) instead of discarding scratch state
	// outside the undo stack - see ModelViewer::replaceToolResults()'s doc
	// comment. Same before-the-call ordering the old discardAllPreviews()
	// call used.
	if (ui->replacePreviousCheckBox->isChecked() && !_lastResultMeshUuids.isEmpty())
	{
		_modelViewer->replaceToolResults(_lastResultMeshUuids, tr("Replace Shrink Wrap Result"));
		_lastResultMeshUuids.clear();
	}

	const QSet<QUuid> originalSelection = _modelViewer->getSelectedUuids();

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

	// Pushed immediately, not deferred to dialog close - matches
	// MeasurementDialog: every result is independently undoable right away
	// (see this class's header doc comment for the bug this fixes).
	_modelViewer->commitShrinkWrap(wrapNode, topParent, wrapPosition, wrappedUuid, originalSelection);
	_lastResultMeshUuids = { wrappedUuid };
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
	saveSettings();
	QDialog::closeEvent(event);
}

void ShrinkWrapDialog::reject()
{
	// Escape reaches here, not closeEvent() (see this override's doc comment
	// in the header) - closeEvent() only does saveSettings() now, so this
	// just needs to make sure Escape doesn't skip it too.
	saveSettings();
	QDialog::reject();
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
