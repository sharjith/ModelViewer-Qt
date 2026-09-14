#include "SurfaceAnalysisDialog.h"
#include "ModelViewer.h"
#include "ViewportWidget.h"
#include "SceneMesh.h"
#include "DraftAngleAnalyzer.h"
#include "DeviationAnalyzer.h"
#include "CurvatureAnalyzer.h"
#include "WallThicknessAnalyzer.h"
#include "AnalysisColorRamp.h"
#include "CoordinateSystemHelper.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QButtonGroup>
#include <QStackedWidget>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QMessageBox>
#include <QApplication>
#include <QIcon>
#include <QCloseEvent>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <QStringList>

SurfaceAnalysisDialog::SurfaceAnalysisDialog(ModelViewer* modelViewer, QWidget* parent)
	: QDialog(parent)
	, _modelViewer(modelViewer)
{
	setWindowTitle(tr("Surface Analysis"));
	resize(420, 420);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(16, 16, 16, 16);
	layout->setSpacing(12);

	auto* introLabel = new QLabel(tr("Analyzes the current selection and paints the result directly on the mesh surface."), this);
	introLabel->setWordWrap(true);
	layout->addWidget(introLabel);

	// Live selection status - this dialog acts on whatever's selected in the
	// scene tree/viewport (not an independent picker of its own), so this
	// reflects that selection directly rather than only ever surfacing
	// "nothing selected" as an error message after the fact when Apply is
	// clicked. Wording depends on the active mode too (Deviation wants
	// exactly one mesh; every other mode acts on the whole selection).
	_selectionStatusLabel = new QLabel(this);
	_selectionStatusLabel->setWordWrap(true);
	layout->addWidget(_selectionStatusLabel);
	if (_modelViewer && _modelViewer->getViewportWidget())
	{
		connect(_modelViewer->getViewportWidget(), &ViewportWidget::selectionChanged,
			this, &SurfaceAnalysisDialog::onSelectionChanged);
		connect(_modelViewer->getViewportWidget(), &ViewportWidget::meshAboutToBeDeleted,
			this, &SurfaceAnalysisDialog::onMeshAboutToBeDeleted);
	}

	// ---- Mode selector: 3-way exclusive icon toggle-button group, not radio
	// buttons or a dropdown - checkable QToolButtons read as switchable
	// "pressed/unpressed" controls rather than small radio dots, which suits
	// an icon-forward selector better; exclusivity/one-active-at-a-time
	// still comes from the same QButtonGroup mechanism radio buttons would
	// have used.
	auto* modeRow = new QHBoxLayout();
	_modeGroup = new QButtonGroup(this);
	_modeGroup->setExclusive(true);

	auto makeModeButton = [this, modeRow](const QString& text, const QString& iconPath, Mode mode) -> QToolButton*
	{
		auto* button = new QToolButton(this);
		button->setText(text);
		button->setIcon(QIcon(iconPath));
		button->setIconSize(QSize(32, 32));
		button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
		button->setCheckable(true);
		button->setAutoRaise(true);
		_modeGroup->addButton(button, static_cast<int>(mode));
		modeRow->addWidget(button);
		return button;
	};

	_curvatureButton = makeModeButton(tr("Curvature"), QStringLiteral(":/icons/res/curvature_analysis.png"), Mode::Curvature);
	_thicknessButton = makeModeButton(tr("Wall-Thickness"), QStringLiteral(":/icons/res/wall_thickness.png"), Mode::WallThickness);
	_deviationButton = makeModeButton(tr("Deviation"), QStringLiteral(":/icons/res/deviation_analysis.png"), Mode::Deviation);

	modeRow->addStretch(1);
	layout->addLayout(modeRow);

	connect(_modeGroup, &QButtonGroup::idClicked, this, &SurfaceAnalysisDialog::onModeChanged);

	// ---- Mode-specific pages ----------------------------------------------
	_stack = new QStackedWidget(this);

	// Curvature page - two independent sub-modes sharing one panel: Zebra
	// Stripe (a live view-dependent shader effect, no legend/color scale)
	// and Mean Curvature (a fixed colormap, like Draft Angle/Deviation).
	// Applying one doesn't automatically clear the other's underlying
	// state, but the shader only ever shows one at a time (the colormap
	// overlay takes priority) - Clear Overlay turns both off together.
	{
		auto* page = new QWidget();
		auto* pageLayout = new QVBoxLayout(page);
		auto* zebraNote = new QLabel(tr("Zebra Stripe reveals surface continuity as a live, view-dependent "
		                                 "reflection pattern - no legend, since it isn't a fixed color scale."), page);
		zebraNote->setWordWrap(true);
		pageLayout->addWidget(zebraNote);
		_zebraStripeToggle = new QPushButton(tr("Zebra Stripe"), page);
		_zebraStripeToggle->setCheckable(true);
		connect(_zebraStripeToggle, &QPushButton::toggled, this, &SurfaceAnalysisDialog::onZebraStripeToggled);
		pageLayout->addWidget(_zebraStripeToggle);

		auto* curvatureNote = new QLabel(tr("Mean Curvature colors each vertex by how sharply the surface "
		                                     "bends there - blue is concave, red is convex, white is flat. "
		                                     "Gaussian/principal curvature modes are not yet available. "
		                                     "Computed on a repaired copy of the mesh (real connectivity is "
		                                     "required); any repair made is disclosed below after Apply."), page);
		curvatureNote->setWordWrap(true);
		pageLayout->addWidget(curvatureNote);
		_applyCurvatureButton = new QPushButton(tr("Apply Mean Curvature"), page);
		connect(_applyCurvatureButton, &QPushButton::clicked, this, &SurfaceAnalysisDialog::onApplyCurvatureClicked);
		pageLayout->addWidget(_applyCurvatureButton);
		_curvatureRepairNote = new QLabel(page);
		_curvatureRepairNote->setWordWrap(true);
		_curvatureRepairNote->setVisible(false);
		pageLayout->addWidget(_curvatureRepairNote);

		pageLayout->addStretch(1);
		_stack->addWidget(page);
	}

	// Wall-Thickness page - two independent sub-modes, same "one panel,
	// applying one doesn't clear the other's state, shader shows whichever
	// was applied last" convention as the Curvature panel.
	{
		auto* page = new QWidget();
		auto* pageLayout = new QVBoxLayout(page);
		auto* draftNote = new QLabel(tr("Draft Angle colors each face by its signed angle to the chosen pull "
		                            "direction - red/positive is an ordinary moldable wall, blue/negative "
		                            "is an undercut, white is parallel to the pull direction (zero draft)."), page);
		draftNote->setWordWrap(true);
		pageLayout->addWidget(draftNote);

		auto* pullRow = new QHBoxLayout();
		pullRow->addWidget(new QLabel(tr("Pull direction:"), page));
		_pullDirectionCombo = new QComboBox(page);
		_pullDirectionCombo->addItem(tr("+X"), QVariant(0));
		_pullDirectionCombo->addItem(tr("-X"), QVariant(1));
		_pullDirectionCombo->addItem(tr("+Y"), QVariant(2));
		_pullDirectionCombo->addItem(tr("-Y"), QVariant(3));
		_pullDirectionCombo->addItem(tr("+Z"), QVariant(4));
		_pullDirectionCombo->addItem(tr("-Z"), QVariant(5));
		// Default selection matches the document's own current world-up
		// axis - the same reasonable default DraftAngleAnalyzer itself
		// falls back to when handed a degenerate direction.
		if (_modelViewer && _modelViewer->getViewportWidget())
		{
			const bool zUp = _modelViewer->getViewportWidget()->isCameraUpAxisZUp();
			_pullDirectionCombo->setCurrentIndex(zUp ? 4 : 2); // +Z or +Y
		}
		pullRow->addWidget(_pullDirectionCombo, 1);
		pageLayout->addLayout(pullRow);

		_applyDraftButton = new QPushButton(tr("Apply Draft Angle"), page);
		connect(_applyDraftButton, &QPushButton::clicked, this, &SurfaceAnalysisDialog::onApplyDraftAngleClicked);
		pageLayout->addWidget(_applyDraftButton);

		auto* thicknessNote = new QLabel(tr("Wall-Thickness colors each face by an inward-ray distance to the "
		                            "opposite wall - blue is thin, red is thick. This is an ESTIMATE, not a "
		                            "guaranteed true minimum (the true minimum can occur along a direction "
		                            "other than the surface normal). Requires a closed, non-self-intersecting "
		                            "mesh that bounds a volume - the whole mesh is rejected with a reason if "
		                            "it doesn't, not partially colored."), page);
		thicknessNote->setWordWrap(true);
		pageLayout->addWidget(thicknessNote);
		_applyThicknessButton = new QPushButton(tr("Apply Wall-Thickness"), page);
		connect(_applyThicknessButton, &QPushButton::clicked, this, &SurfaceAnalysisDialog::onApplyWallThicknessClicked);
		pageLayout->addWidget(_applyThicknessButton);
		_thicknessRejectionNote = new QLabel(page);
		_thicknessRejectionNote->setWordWrap(true);
		_thicknessRejectionNote->setVisible(false);
		pageLayout->addWidget(_thicknessRejectionNote);

		pageLayout->addStretch(1);
		_stack->addWidget(page);
	}

	// Deviation page - unsigned nearest-surface distance from the selected
	// (scan/comparison) mesh to a chosen reference mesh.
	{
		auto* page = new QWidget();
		auto* pageLayout = new QVBoxLayout(page);
		auto* note = new QLabel(tr("Colors the selected mesh by its unsigned distance to a reference mesh's "
		                            "surface - dark blue is a close match, red is the largest deviation found. "
		                            "Both meshes must already be aligned in the same coordinate frame and use "
		                            "the same units; a plain offset between them will read as a false deviation. "
		                            "This is a sampled result (measured per vertex), not exhaustive coverage."), page);
		note->setWordWrap(true);
		pageLayout->addWidget(note);

		auto* refRow = new QHBoxLayout();
		refRow->addWidget(new QLabel(tr("Reference mesh:"), page));
		_referenceMeshCombo = new QComboBox(page);
		refRow->addWidget(_referenceMeshCombo, 1);
		pageLayout->addLayout(refRow);

		_applyDeviationButton = new QPushButton(tr("Apply"), page);
		connect(_applyDeviationButton, &QPushButton::clicked, this, &SurfaceAnalysisDialog::onApplyDeviationClicked);
		pageLayout->addWidget(_applyDeviationButton);
		pageLayout->addStretch(1);
		_stack->addWidget(page);
	}

	layout->addWidget(_stack, 1);

	_legendLabel = new QLabel(this);
	_legendLabel->setAlignment(Qt::AlignCenter);
	_legendLabel->setVisible(false);
	layout->addWidget(_legendLabel);

	auto* bottomRow = new QHBoxLayout();
	_clearButton = new QPushButton(tr("Clear Overlay"), this);
	connect(_clearButton, &QPushButton::clicked, this, &SurfaceAnalysisDialog::onClearClicked);
	bottomRow->addWidget(_clearButton);
	bottomRow->addStretch(1);
	auto* closeButton = new QPushButton(tr("Close"), this);
	// close(), not accept()/QDialog::done() - done() only hide()s the dialog,
	// it never reaches closeEvent(), which is where the overlay auto-clear
	// (and WA_DeleteOnClose's actual deletion) happens. See closeEvent()'s
	// doc comment.
	connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
	bottomRow->addWidget(closeButton);
	layout->addLayout(bottomRow);

	_curvatureButton->setChecked(true);
	onModeChanged();
}

void SurfaceAnalysisDialog::closeEvent(QCloseEvent* event)
{
	// See this class's header doc comment - closing (via Close, the window's
	// X button, or any other path) always clears everything this dialog
	// applied, since WA_DeleteOnClose means there's no "Clear Overlay"
	// button left to press afterward.
	clearAllOverlays();
	QDialog::closeEvent(event);
}

void SurfaceAnalysisDialog::reject()
{
	// Escape doesn't route through closeEvent() (QDialog::reject() only
	// hide()s) - same double-override pattern MeasurementDialog already
	// uses for this exact reason.
	clearAllOverlays();
	QDialog::reject();
}

SurfaceAnalysisDialog::Mode SurfaceAnalysisDialog::currentMode() const
{
	if (_thicknessButton->isChecked())
		return Mode::WallThickness;
	if (_deviationButton->isChecked())
		return Mode::Deviation;
	return Mode::Curvature;
}

void SurfaceAnalysisDialog::onModeChanged()
{
	const Mode mode = currentMode();
	_stack->setCurrentIndex(static_cast<int>(mode));
	// The legend only means something for a fixed-scale colormapped result
	// (Draft Angle, Mean Curvature, Deviation) - hide it on every mode
	// switch so a stale legend from a previous run doesn't linger looking
	// like it applies to whatever's now showing. Re-shown by each apply*()
	// method itself once a result actually exists (Zebra Stripe never shows
	// one - it's a live view-dependent effect, not a fixed color scale).
	_legendLabel->setVisible(false);
	if (_curvatureRepairNote)
		_curvatureRepairNote->setVisible(false);
	if (_thicknessRejectionNote)
		_thicknessRejectionNote->setVisible(false);

	if (mode == Mode::Deviation)
		refreshReferenceMeshCombo();

	updateSelectionStatusLabel();
}

void SurfaceAnalysisDialog::onSelectionChanged()
{
	// The Deviation page's reference-mesh choices exclude whatever's
	// selected (see refreshReferenceMeshCombo()'s doc comment) - that set
	// changes whenever the selection does, so it needs the same refresh.
	if (currentMode() == Mode::Deviation)
		refreshReferenceMeshCombo();

	updateSelectionStatusLabel();
}

void SurfaceAnalysisDialog::onMeshAboutToBeDeleted(SceneMesh* mesh)
{
	if (!mesh)
		return;

	// The mesh is still alive right now (see the signal's own doc comment) -
	// clearOverlay() safely calls mesh->clearAnalysisOverlay() one last time
	// before dropping this dialog's own tracking of it. No GL call needed
	// for zebra-stripe - the mesh (and its GL resources) are going away
	// regardless, there's nothing left to turn off.
	_overlay.clearOverlay(mesh);
	_zebraStripeMeshes.remove(mesh);
}

void SurfaceAnalysisDialog::updateSelectionStatusLabel()
{
	if (!_selectionStatusLabel || !_modelViewer)
		return;

	const std::vector<int> selected = _modelViewer->getSelectedIDs();
	const Mode mode = currentMode();

	if (selected.empty())
	{
		_selectionStatusLabel->setText(tr("No mesh selected - select one or more meshes in the scene tree first."));
		return;
	}

	if (mode == Mode::Deviation && selected.size() != 1)
	{
		_selectionStatusLabel->setText(tr("%1 meshes selected - Deviation needs exactly one (the scan/comparison side).")
			.arg(static_cast<int>(selected.size())));
		return;
	}

	ViewportWidget* viewport = _modelViewer->getViewportWidget();
	const std::vector<SceneMesh*> meshStore = viewport ? viewport->getMeshStore() : std::vector<SceneMesh*>();
	QStringList names;
	names.reserve(static_cast<int>(selected.size()));
	for (int id : selected)
	{
		if (id >= 0 && static_cast<size_t>(id) < meshStore.size() && meshStore[id])
			names.append(meshStore[id]->getName());
	}

	if (mode == Mode::Deviation)
	{
		_selectionStatusLabel->setText(tr("Comparing: %1").arg(names.value(0)));
	}
	else
	{
		_selectionStatusLabel->setText(selected.size() == 1
			? tr("Selected: %1").arg(names.value(0))
			: tr("Selected (%1): %2").arg(static_cast<int>(selected.size())).arg(names.join(QStringLiteral(", "))));
	}
}

QVector3D SurfaceAnalysisDialog::currentPullDirection() const
{
	if (!_pullDirectionCombo)
		return QVector3D(0.0f, 1.0f, 0.0f);
	switch (_pullDirectionCombo->currentIndex())
	{
	case 0: return QVector3D(1.0f, 0.0f, 0.0f);
	case 1: return QVector3D(-1.0f, 0.0f, 0.0f);
	case 2: return QVector3D(0.0f, 1.0f, 0.0f);
	case 3: return QVector3D(0.0f, -1.0f, 0.0f);
	case 4: return QVector3D(0.0f, 0.0f, 1.0f);
	case 5: return QVector3D(0.0f, 0.0f, -1.0f);
	default: return QVector3D(0.0f, 1.0f, 0.0f);
	}
}

void SurfaceAnalysisDialog::onZebraStripeToggled(bool checked)
{
	applyZebraStripeToSelection(checked);
}

void SurfaceAnalysisDialog::onApplyDraftAngleClicked()
{
	applyDraftAngleToSelection();
}

void SurfaceAnalysisDialog::onApplyDeviationClicked()
{
	applyDeviationToSelection();
}

void SurfaceAnalysisDialog::onApplyCurvatureClicked()
{
	applyCurvatureToSelection();
}

void SurfaceAnalysisDialog::onApplyWallThicknessClicked()
{
	applyWallThicknessToSelection();
}

void SurfaceAnalysisDialog::onClearClicked()
{
	clearSelectionOverlays();
}

void SurfaceAnalysisDialog::applyZebraStripeToSelection(bool active)
{
	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	if (!viewport)
		return;

	const std::vector<int> selected = _modelViewer->getSelectedIDs();
	if (selected.empty())
	{
		if (active)
		{
			QMessageBox::information(this, tr("Surface Analysis"), tr("Select one or more meshes first."));
			_zebraStripeToggle->setChecked(false);
		}
		return;
	}

	const std::vector<SceneMesh*> meshStore = viewport->getMeshStore();
	for (int id : selected)
	{
		SceneMesh* mesh = meshStore.at(id);
		mesh->setZebraStripeActive(active);
		if (active)
			_zebraStripeMeshes.insert(mesh);
		else
			_zebraStripeMeshes.remove(mesh);
	}

	viewport->update();
}

void SurfaceAnalysisDialog::applyCurvatureToSelection()
{
	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	if (!viewport)
		return;

	const std::vector<int> selected = _modelViewer->getSelectedIDs();
	if (selected.empty())
	{
		QMessageBox::information(this, tr("Surface Analysis"), tr("Select one or more meshes first."));
		return;
	}

	const std::vector<SceneMesh*> meshStore = viewport->getMeshStore();

	struct PerMesh { SceneMesh* mesh; CurvatureResult result; };
	std::vector<PerMesh> perMesh;
	perMesh.reserve(selected.size());

	float bound = 0.0f;
	bool anyValid = false;
	QStringList repairNotes;

	// Cheap interim mitigation, not real async - see MassPropertiesDialog's
	// own identical note. CurvatureAnalyzer's repair + interpolated-
	// corrected-curvatures + per-vertex AABB locate below is the heaviest of
	// this dialog's three analyzers and runs entirely on the UI thread.
	QApplication::setOverrideCursor(Qt::WaitCursor);
	for (int id : selected)
	{
		SceneMesh* mesh = meshStore.at(id);
		CurvatureResult curvature = CurvatureAnalyzer::computeMeanCurvature(mesh);
		if (curvature.succeeded)
		{
			repairNotes.append(QStringLiteral("%1: %2").arg(mesh->getName(), curvature.repairSummary));
			for (size_t i = 0; i < curvature.meanCurvaturePerVertex.size(); ++i)
			{
				if (curvature.validPerVertex[i])
				{
					bound = std::max(bound, std::fabs(curvature.meanCurvaturePerVertex[i]));
					anyValid = true;
				}
			}
		}
		perMesh.push_back({ mesh, std::move(curvature) });
	}
	QApplication::restoreOverrideCursor();

	const float rangeMin = bound > 1.0e-6f ? -bound : -1.0f;
	const float rangeMax = bound > 1.0e-6f ? bound : 1.0f;

	// setAnalysisOverlayColors() below uploads a real GPU buffer - same
	// makeCurrent()/doneCurrent() reasoning as every other Apply here. Runs
	// for EVERY mesh regardless of anyValid below (including the all-failed
	// case) - a mesh whose curvature computation failed must have whatever
	// UNRELATED overlay it happened to already be showing (e.g. a Draft
	// Angle result from an earlier Apply) cleared too, not left silently
	// displayed under this run's new (curvature-scaled) legend.
	viewport->makeCurrent();
	for (PerMesh& pm : perMesh)
	{
		if (!pm.result.succeeded)
		{
			_overlay.clearOverlay(pm.mesh);
			continue;
		}

		SurfaceAnalysisOverlay::CacheKey key;
		key.geometryRevision = pm.mesh->geometryRevision();
		key.transform = pm.mesh->combinedRenderTransform();
		QVariantMap params;
		params.insert(QStringLiteral("mode"), QStringLiteral("meanCurvature"));
		key.parameters = params;

		_overlay.applyResult(pm.mesh, pm.result.meanCurvaturePerVertex, pm.result.validPerVertex,
			key, rangeMin, rangeMax, AnalysisColormap::Diverging);
	}
	viewport->doneCurrent();
	viewport->update();

	if (!anyValid)
	{
		QMessageBox::warning(this, tr("Surface Analysis"),
			tr("Could not compute a usable curvature result for the current selection."));
		return;
	}

	_legendLabel->setPixmap(AnalysisColorRamp::legendGradient(280, 44, rangeMin, rangeMax, AnalysisColormap::Diverging, QString()));
	_legendLabel->setVisible(true);

	if (_curvatureRepairNote)
	{
		_curvatureRepairNote->setText(repairNotes.join(QStringLiteral("\n")));
		_curvatureRepairNote->setVisible(true);
	}
}

void SurfaceAnalysisDialog::applyWallThicknessToSelection()
{
	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	if (!viewport)
		return;

	const std::vector<int> selected = _modelViewer->getSelectedIDs();
	if (selected.empty())
	{
		QMessageBox::information(this, tr("Surface Analysis"), tr("Select one or more meshes first."));
		return;
	}

	const std::vector<SceneMesh*> meshStore = viewport->getMeshStore();

	struct PerMesh { SceneMesh* mesh; WallThicknessResult result; };
	std::vector<PerMesh> perMesh;
	perMesh.reserve(selected.size());

	float maxThickness = 0.0f;
	bool anyValid = false;
	QStringList rejectionNotes;

	// Cheap interim mitigation, not real async - see MassPropertiesDialog's
	// own identical note. This is the heaviest of this dialog's four
	// analyses (whole-mesh topology validation, orientation resolution,
	// solid-region classification, then a full AABB-tree multi-hit ray per
	// face) and runs entirely on the UI thread below.
	QApplication::setOverrideCursor(Qt::WaitCursor);
	for (int id : selected)
	{
		SceneMesh* mesh = meshStore.at(id);
		WallThicknessResult thickness = WallThicknessAnalyzer::computeThickness(mesh);
		if (thickness.succeeded)
		{
			for (size_t i = 0; i < thickness.thicknessPerFace.size(); ++i)
			{
				if (thickness.validPerFace[i])
				{
					maxThickness = std::max(maxThickness, thickness.thicknessPerFace[i]);
					anyValid = true;
				}
			}
		}
		else
		{
			rejectionNotes.append(QStringLiteral("%1: %2").arg(mesh->getName(), thickness.rejectionReason));
		}
		perMesh.push_back({ mesh, std::move(thickness) });
	}
	QApplication::restoreOverrideCursor();

	if (_thicknessRejectionNote)
	{
		_thicknessRejectionNote->setText(rejectionNotes.join(QStringLiteral("\n")));
		_thicknessRejectionNote->setVisible(!rejectionNotes.isEmpty());
	}

	// 0 is the natural bottom of the range (zero thickness) rather than the
	// sampled data's own minimum - same reasoning as Deviation's range.
	const float rangeMax = maxThickness > 1.0e-6f ? maxThickness : 1.0f;

	// setAnalysisOverlayFlatColors() below uploads a real GPU buffer - same
	// makeCurrent()/doneCurrent() reasoning as every other Apply here. Runs
	// for EVERY mesh regardless of anyValid below (including the all-
	// rejected case) - a REJECTED mesh must have whatever UNRELATED overlay
	// it happened to already be showing (e.g. a Draft Angle result from an
	// earlier Apply) cleared too, not left silently displayed under this
	// run's new (thickness-scaled) legend.
	viewport->makeCurrent();
	for (PerMesh& pm : perMesh)
	{
		if (!pm.result.succeeded)
		{
			_overlay.clearOverlay(pm.mesh);
			continue;
		}

		SurfaceAnalysisOverlay::CacheKey key;
		key.geometryRevision = pm.mesh->geometryRevision();
		key.transform = pm.mesh->combinedRenderTransform();
		QVariantMap params;
		params.insert(QStringLiteral("mode"), QStringLiteral("wallThickness"));
		key.parameters = params;

		_overlay.applyFlatResult(pm.mesh, pm.result.thicknessPerFace, pm.result.validPerFace,
			key, 0.0f, rangeMax, AnalysisColormap::Sequential);
	}
	viewport->doneCurrent();
	viewport->update();

	if (!anyValid)
	{
		if (rejectionNotes.isEmpty())
		{
			QMessageBox::warning(this, tr("Surface Analysis"),
				tr("Could not compute wall thickness - no face found a valid opposite-wall hit."));
		}
		return;
	}

	_legendLabel->setPixmap(AnalysisColorRamp::legendGradient(280, 44, 0.0f, rangeMax, AnalysisColormap::Sequential, QString()));
	_legendLabel->setVisible(true);
}

void SurfaceAnalysisDialog::applyDraftAngleToSelection()
{
	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	if (!viewport)
		return;

	const std::vector<int> selected = _modelViewer->getSelectedIDs();
	if (selected.empty())
	{
		QMessageBox::information(this, tr("Surface Analysis"), tr("Select one or more meshes first."));
		return;
	}

	const QVector3D pullDirection = currentPullDirection();
	const std::vector<SceneMesh*> meshStore = viewport->getMeshStore();

	struct PerMesh { SceneMesh* mesh; std::vector<float> angles; };
	std::vector<PerMesh> perMesh;
	perMesh.reserve(selected.size());

	float minAngle = std::numeric_limits<float>::max();
	float maxAngle = std::numeric_limits<float>::lowest();
	bool anyFace = false;

	// Cheap interim mitigation, not real async - see MassPropertiesDialog's
	// own identical note. Draft Angle's per-face math is the lightest of
	// this dialog's three analyzers (no CGAL/AABB involved), but still runs
	// on the UI thread and can take a moment on a very dense mesh.
	QApplication::setOverrideCursor(Qt::WaitCursor);
	for (int id : selected)
	{
		SceneMesh* mesh = meshStore.at(id);
		std::vector<float> angles = DraftAngleAnalyzer::computeDraftAnglesDegrees(mesh, pullDirection);
		for (float a : angles)
		{
			minAngle = std::min(minAngle, a);
			maxAngle = std::max(maxAngle, a);
			anyFace = true;
		}
		perMesh.push_back({ mesh, std::move(angles) });
	}
	QApplication::restoreOverrideCursor();

	if (!anyFace)
		return;

	// Symmetric range around zero so the Diverging colormap's white midpoint
	// genuinely represents zero draft (a wall parallel to the pull
	// direction), not an arbitrary data-dependent value that would make
	// "white" mean something different for every run.
	const float bound = std::max(std::fabs(minAngle), std::fabs(maxAngle));
	const float rangeMin = bound > 1.0e-4f ? -bound : -1.0f;
	const float rangeMax = bound > 1.0e-4f ? bound : 1.0f;

	// applyFlatResult() -> setAnalysisOverlayFlatColors() below creates/
	// uploads real GPU buffers and a VAO - this slot runs from a button
	// click, not from inside paintGL(), so there is no guarantee the
	// viewport's context is current. Same makeCurrent()/doneCurrent()
	// pairing every other mesh-mutating dialog follows (ShrinkWrapDialog,
	// FillHolesDialog, RepairMeshDialog, ...) - without it, VAO creation/
	// binding can silently target the wrong (or no) context, which is
	// consistent with the black-overlay symptom this fixes: a VAO created
	// outside the real context isn't valid when later bound during an
	// actual paintGL() draw call.
	viewport->makeCurrent();
	for (PerMesh& pm : perMesh)
	{
		SurfaceAnalysisOverlay::CacheKey key;
		key.geometryRevision = pm.mesh->geometryRevision();
		key.transform = pm.mesh->combinedRenderTransform();
		QVariantMap params;
		params.insert(QStringLiteral("mode"), QStringLiteral("draftAngle"));
		params.insert(QStringLiteral("pullDirection"), QVariant::fromValue(pullDirection));
		key.parameters = params;

		_overlay.applyFlatResult(pm.mesh, pm.angles, {}, key, rangeMin, rangeMax, AnalysisColormap::Diverging);
	}
	viewport->doneCurrent();

	_legendLabel->setPixmap(AnalysisColorRamp::legendGradient(280, 44, rangeMin, rangeMax, AnalysisColormap::Diverging, QStringLiteral("°")));
	_legendLabel->setVisible(true);

	viewport->update();
}

void SurfaceAnalysisDialog::refreshReferenceMeshCombo()
{
	if (!_referenceMeshCombo || !_modelViewer)
		return;

	ViewportWidget* viewport = _modelViewer->getViewportWidget();
	if (!viewport)
		return;

	// Preserve the previously chosen reference mesh across a refresh, where
	// it's still a valid choice - by UUID, not mesh-store index: an index
	// captured here can point at a different mesh entirely by the time
	// applyDeviationToSelection() reads it back (an import/delete reindexes
	// the store in between), silently comparing against the wrong mesh
	// while the combo still shows the original name.
	const QUuid previousUuid = _referenceMeshCombo->count() > 0
		? _referenceMeshCombo->currentData().toUuid() : QUuid();

	_referenceMeshCombo->clear();

	const std::vector<int> selected = _modelViewer->getSelectedIDs();
	const std::vector<SceneMesh*> meshStore = viewport->getMeshStore();
	for (size_t id = 0; id < meshStore.size(); ++id)
	{
		SceneMesh* mesh = meshStore[id];
		if (!mesh)
			continue;
		// Excludes whatever's currently selected - that's the scan/
		// comparison side being measured, not a valid reference for itself.
		if (std::find(selected.begin(), selected.end(), static_cast<int>(id)) != selected.end())
			continue;
		_referenceMeshCombo->addItem(mesh->getName(), QVariant(mesh->uuid()));
	}

	if (!previousUuid.isNull())
	{
		const int idx = _referenceMeshCombo->findData(QVariant(previousUuid));
		if (idx >= 0)
			_referenceMeshCombo->setCurrentIndex(idx);
	}
}

void SurfaceAnalysisDialog::applyDeviationToSelection()
{
	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	if (!viewport)
		return;

	// Exactly one mesh, unlike Draft Angle's whole-selection convention -
	// deviation is inherently pairwise (one scan/comparison mesh against one
	// reference), so "compare A vs B" doesn't generalize to a multi-mesh
	// selection the way a per-mesh-independent analysis does.
	const std::vector<int> selected = _modelViewer->getSelectedIDs();
	if (selected.size() != 1)
	{
		QMessageBox::information(this, tr("Surface Analysis"),
			tr("Select exactly one mesh to compare (the scan/comparison side)."));
		return;
	}
	if (!_referenceMeshCombo || _referenceMeshCombo->count() == 0)
	{
		QMessageBox::information(this, tr("Surface Analysis"),
			tr("No other loaded mesh is available to compare against."));
		return;
	}

	const std::vector<SceneMesh*> meshStore = viewport->getMeshStore();
	SceneMesh* sampledMesh = meshStore.at(selected.front());
	// Resolved by UUID, not the mesh-store index the combo used to store -
	// see refreshReferenceMeshCombo()'s doc comment. getMeshByUuid() returns
	// nullptr if the chosen mesh was deleted since the combo was populated,
	// which the empty-selection message below covers well enough (no need
	// for a separate error string for this specific case).
	const QUuid referenceUuid = _referenceMeshCombo->currentData().toUuid();
	SceneMesh* referenceMesh = referenceUuid.isNull() ? nullptr : viewport->getMeshByUuid(referenceUuid);
	if (!referenceMesh || referenceMesh == sampledMesh)
	{
		QMessageBox::information(this, tr("Surface Analysis"),
			tr("The chosen reference mesh is no longer available - pick another one."));
		return;
	}

	// Cheap interim mitigation, not real async - see MassPropertiesDialog's
	// own identical note. AABB-tree construction + a nearest-point query per
	// sampled vertex run entirely on the UI thread below.
	QApplication::setOverrideCursor(Qt::WaitCursor);
	const std::vector<float> distances = DeviationAnalyzer::computeDeviation(sampledMesh, referenceMesh);
	QApplication::restoreOverrideCursor();
	if (distances.empty())
	{
		QMessageBox::warning(this, tr("Surface Analysis"),
			tr("Could not compute deviation - the reference mesh has no usable triangles."));
		return;
	}

	// 0 is always the natural bottom of the range (a perfect match) rather
	// than the sampled data's own minimum - an unsigned distance field's
	// zero point is physically meaningful, unlike Draft Angle's data-
	// dependent symmetric bound.
	float maxDist = 0.0f;
	for (float d : distances)
		maxDist = std::max(maxDist, d);
	const float rangeMax = maxDist > 1.0e-6f ? maxDist : 1.0f;

	SurfaceAnalysisOverlay::CacheKey key;
	key.geometryRevision = sampledMesh->geometryRevision();
	key.transform = sampledMesh->combinedRenderTransform();
	key.referenceMesh = referenceMesh;
	key.referenceGeometryRevision = referenceMesh->geometryRevision();
	key.referenceTransform = referenceMesh->combinedRenderTransform();
	QVariantMap params;
	params.insert(QStringLiteral("mode"), QStringLiteral("deviation"));
	key.parameters = params;

	// setAnalysisOverlayColors() below uploads a real GPU buffer - same
	// makeCurrent()/doneCurrent() reasoning as applyDraftAngleToSelection().
	viewport->makeCurrent();
	_overlay.applyResult(sampledMesh, distances, {}, key, 0.0f, rangeMax, AnalysisColormap::Sequential);
	viewport->doneCurrent();

	_legendLabel->setPixmap(AnalysisColorRamp::legendGradient(280, 44, 0.0f, rangeMax, AnalysisColormap::Sequential, QString()));
	_legendLabel->setVisible(true);

	viewport->update();
}

void SurfaceAnalysisDialog::clearSelectionOverlays()
{
	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	if (!viewport)
		return;

	const std::vector<int> selected = _modelViewer->getSelectedIDs();
	const std::vector<SceneMesh*> meshStore = viewport->getMeshStore();
	for (int id : selected)
	{
		SceneMesh* mesh = meshStore.at(id);
		mesh->setZebraStripeActive(false);
		_zebraStripeMeshes.remove(mesh);
		_overlay.clearOverlay(mesh);
	}

	_zebraStripeToggle->blockSignals(true);
	_zebraStripeToggle->setChecked(false);
	_zebraStripeToggle->blockSignals(false);
	_legendLabel->setVisible(false);
	if (_curvatureRepairNote)
		_curvatureRepairNote->setVisible(false);
	if (_thicknessRejectionNote)
		_thicknessRejectionNote->setVisible(false);

	viewport->update();
}

void SurfaceAnalysisDialog::clearAllOverlays()
{
	// Unlike clearSelectionOverlays() (the "Clear Overlay" button, scoped to
	// whatever's currently selected), this clears every mesh this dialog
	// ever touched, whether or not it's still selected - see this method's
	// declaration comment.
	for (SceneMesh* mesh : std::as_const(_zebraStripeMeshes))
	{
		if (mesh)
			mesh->setZebraStripeActive(false);
	}
	_zebraStripeMeshes.clear();

	_overlay.clearAll();

	if (ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr)
		viewport->update();
}
