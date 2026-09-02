#include "ReconstructSurfaceDialog.h"
#include "ui_ReconstructSurfaceDialog.h"

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
#include <QtMath>

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

	// Highest N found among direct top-level nodes named "Reconstruct Surface
	// NNN" (0 if none) - lets a fresh dialog/document keep numbering forward
	// instead of restarting at 1 and colliding with an already-committed name.
	int highestExistingReconstructIndex(SceneNode* root)
	{
		if (!root)
			return 0;
		static const QRegularExpression pattern(QStringLiteral("^Reconstruct Surface (\\d+)$"));
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

ReconstructSurfaceDialog::ReconstructSurfaceDialog(ModelViewer* modelViewer, QWidget* parent)
	: QDialog(parent)
	, _modelViewer(modelViewer)
	, ui(std::make_unique<Ui::ReconstructSurfaceDialog>())
{
	ui->setupUi(this);
	setAttribute(Qt::WA_DeleteOnClose);

	connect(ui->addSelectedButton, &QPushButton::clicked, this, &ReconstructSurfaceDialog::addCurrentTreeSelection);
	connect(ui->removeSelectedButton, &QPushButton::clicked, this, &ReconstructSurfaceDialog::onRemoveSelectedClicked);
	connect(ui->resetToleranceButton, &QPushButton::clicked, this, &ReconstructSurfaceDialog::onResetToleranceClicked);
	connect(ui->simplifyCheckBox, &QCheckBox::toggled, this, &ReconstructSurfaceDialog::onSimplifyToggled);
	connect(ui->generateButton, &QPushButton::clicked, this, &ReconstructSurfaceDialog::onGenerateClicked);
	connect(ui->meshList, &QListWidget::itemSelectionChanged, this, &ReconstructSurfaceDialog::onListSelectionChanged);

	if (_modelViewer->sceneGraph())
		_nextReconstructIndex = highestExistingReconstructIndex(_modelViewer->sceneGraph()->root()) + 1;

	updateActionButtonsEnabled();
	loadSettings();
}

ReconstructSurfaceDialog::~ReconstructSurfaceDialog()
{
}

void ReconstructSurfaceDialog::addCurrentTreeSelection()
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
		refreshSuggestedSpacing();
	updateActionButtonsEnabled();
}

void ReconstructSurfaceDialog::onRemoveSelectedClicked()
{
	qDeleteAll(ui->meshList->selectedItems());
	updateActionButtonsEnabled();
}

void ReconstructSurfaceDialog::onListSelectionChanged()
{
	ui->removeSelectedButton->setEnabled(!ui->meshList->selectedItems().isEmpty());
}

void ReconstructSurfaceDialog::updateActionButtonsEnabled()
{
	const bool hasMeshes = ui->meshList->count() > 0;
	ui->generateButton->setEnabled(hasMeshes);
	ui->resetToleranceButton->setEnabled(hasMeshes);
}

void ReconstructSurfaceDialog::onResetToleranceClicked()
{
	refreshSuggestedSpacing();
}

void ReconstructSurfaceDialog::onSimplifyToggled(bool checked)
{
	ui->spacingSpin->setEnabled(checked);
	if (checked && ui->spacingSpin->value() <= 0.0)
		refreshSuggestedSpacing();
}

void ReconstructSurfaceDialog::refreshSuggestedSpacing()
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

	double spacing = 0.0;
	SceneMesh::suggestReconstructionSpacing(meshes, spacing);
	if (spacing > 0.0)
		ui->spacingSpin->setValue(spacing);
}

void ReconstructSurfaceDialog::onGenerateClicked()
{
	ViewportWidget* viewport = _modelViewer->getViewportWidget();
	SceneGraph* sceneGraph = _modelViewer->sceneGraph();
	if (!viewport || !sceneGraph)
		return;

	QVector<SceneMesh*> meshes;
	int totalPoints = 0;
	for (int i = 0; i < ui->meshList->count(); ++i)
	{
		SceneMesh* mesh = viewport->getMeshByUuid(ui->meshList->item(i)->data(Qt::UserRole).toUuid());
		if (mesh)
		{
			meshes.append(mesh);
			totalPoints += static_cast<int>(mesh->getTrsfPoints().size() / 3);
		}
	}

	if (meshes.isEmpty())
	{
		ui->statusLabel->setText(tr("Add at least one mesh to the list first."));
		return;
	}

	// Reconstruction cost scales with input size (a full Delaunay
	// triangulation of every point) and is synchronous/non-cancellable,
	// same as alpha_wrap_3 - give the user a heads-up before committing to a
	// potentially long call on a large scan, rather than just going quiet.
	constexpr int kLargePointCountWarningThreshold = 200000;
	ui->statusLabel->setText(totalPoints > kLargePointCountWarningThreshold
		? tr("Reconstructing %1 points - this may take a while...").arg(totalPoints)
		: tr("Generating..."));
	ui->generateButton->setEnabled(false);
	MainWindow::showIndeterminateProgressBar();
	MainWindow::setCancelButtonEnabled(false); // no cancellation mid-call is possible
	QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

	// "Replace previous result" undoably deletes the last result (one real
	// DeleteMeshCommand push) - same convention as ShrinkWrapDialog/
	// SubdivisionDialog, see ModelViewer::replaceToolResults()'s doc comment.
	if (ui->replacePreviousCheckBox->isChecked() && !_lastResultMeshUuids.isEmpty())
	{
		_modelViewer->replaceToolResults(_lastResultMeshUuids, tr("Replace Reconstructed Surface"));
		_lastResultMeshUuids.clear();
	}

	const QSet<QUuid> originalSelection = _modelViewer->getSelectedUuids();

	viewport->makeCurrent();

	const QString reconName = QStringLiteral("Reconstruct Surface %1").arg(_nextReconstructIndex, 3, 10, QChar('0'));
	const QString meshName = viewport->generateUniqueMeshName(reconName);
	const double beta = qDegreesToRadians(ui->sharpnessSpin->value());
	const double simplifySpacing = ui->simplifyCheckBox->isChecked() ? ui->spacingSpin->value() : 0.0;
	SceneMesh* reconstructed = SceneMesh::reconstructSurfaceFromPoints(
		meshes, meshName, ui->boundaryToleranceSpin->value(), beta, simplifySpacing);
	if (!reconstructed)
	{
		viewport->doneCurrent();
		MainWindow::hideProgressBar();
		ui->generateButton->setEnabled(true);
		ui->statusLabel->setText(tr("Reconstruction failed - no geometry was produced."));
		return;
	}
	viewport->addToDisplay(reconstructed);
	const QUuid reconstructedUuid = reconstructed->uuid();

	SceneNode* reconNode = new SceneNode();
	reconNode->nodeUuid = QUuid::createUuid();
	reconNode->name = reconName;

	SceneNode* topParent = sceneGraph->root();
	const int reconPosition = topParent->children.size();
	sceneGraph->insertChildNode(topParent, reconNode, reconPosition);
	sceneGraph->restoreMeshUuid(reconNode, reconstructedUuid, 0);

	viewport->doneCurrent();
	viewport->updateView();
	_modelViewer->updateDisplayList();

	// Pushed immediately, not deferred to dialog close - matches
	// MeasurementDialog/ShrinkWrapDialog/SubdivisionDialog: every result is
	// independently undoable right away (see this class's header doc
	// comment for the bug this fixes).
	_modelViewer->commitReconstructSurface(reconNode, topParent, reconPosition, reconstructedUuid, originalSelection);
	_lastResultMeshUuids = { reconstructedUuid };
	++_nextReconstructIndex;

	MainWindow::hideProgressBar();
	ui->generateButton->setEnabled(true);
	ui->statusLabel->setText(tr("%1: %2 point(s) -> %3 vertices, %4 triangles.")
	                              .arg(reconName)
	                              .arg(totalPoints)
	                              .arg(reconstructed->vertices().size())
	                              .arg(reconstructed->indices().size() / 3));
}

void ReconstructSurfaceDialog::closeEvent(QCloseEvent* event)
{
	saveSettings();
	QDialog::closeEvent(event);
}

void ReconstructSurfaceDialog::reject()
{
	// Escape reaches here, not closeEvent() (see this override's doc comment
	// in the header) - closeEvent() only does saveSettings() now, so this
	// just needs to make sure Escape doesn't skip it too.
	saveSettings();
	QDialog::reject();
}

void ReconstructSurfaceDialog::loadSettings()
{
	QSettings settings;
	const QByteArray geometry = settings.value("reconstructSurface/geometry", QByteArray()).toByteArray();
	if (!geometry.isEmpty())
		restoreGeometry(geometry);
}

void ReconstructSurfaceDialog::saveSettings()
{
	QSettings settings;
	settings.setValue("reconstructSurface/geometry", saveGeometry());
}
