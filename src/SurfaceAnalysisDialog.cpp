#include "SurfaceAnalysisDialog.h"
#include "ModelViewer.h"
#include "ViewportWidget.h"
#include "SceneMesh.h"
#include "DraftAngleAnalyzer.h"
#include "DeviationAnalyzer.h"
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
#include <QIcon>
#include <QCloseEvent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

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

	// Curvature page - only Zebra Stripe is implemented so far; the
	// colormap sub-mode is a later step (see this class's own header
	// comment).
	{
		auto* page = new QWidget();
		auto* pageLayout = new QVBoxLayout(page);
		auto* note = new QLabel(tr("Curvature colormap analysis is not yet available. "
		                            "Zebra Stripe reveals surface continuity as a live, view-dependent "
		                            "reflection pattern - no legend, since it isn't a fixed color scale."), page);
		note->setWordWrap(true);
		pageLayout->addWidget(note);
		_zebraStripeToggle = new QPushButton(tr("Zebra Stripe"), page);
		_zebraStripeToggle->setCheckable(true);
		connect(_zebraStripeToggle, &QPushButton::toggled, this, &SurfaceAnalysisDialog::onZebraStripeToggled);
		pageLayout->addWidget(_zebraStripeToggle);
		pageLayout->addStretch(1);
		_stack->addWidget(page);
	}

	// Wall-Thickness page - only Draft Angle is implemented so far; true
	// wall-thickness (ray-based) is a later, harder step (see this class's
	// own header comment).
	{
		auto* page = new QWidget();
		auto* pageLayout = new QVBoxLayout(page);
		auto* note = new QLabel(tr("Wall-thickness analysis is not yet available. Draft Angle colors each "
		                            "face by its signed angle to the chosen pull direction - green/positive "
		                            "is an ordinary moldable wall, red/negative is an undercut."), page);
		note->setWordWrap(true);
		pageLayout->addWidget(note);

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

		_applyDraftButton = new QPushButton(tr("Apply"), page);
		connect(_applyDraftButton, &QPushButton::clicked, this, &SurfaceAnalysisDialog::onApplyDraftAngleClicked);
		pageLayout->addWidget(_applyDraftButton);
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
	// (Draft Angle, Deviation) - hide it for Curvature (Zebra Stripe has no
	// scale) so a stale legend from a previous run doesn't linger looking
	// like it applies here. Re-shown by applyDraftAngleToSelection()/
	// applyDeviationToSelection() themselves once a result actually exists.
	_legendLabel->setVisible(false);

	if (mode == Mode::Deviation)
		refreshReferenceMeshCombo();
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

	// Preserve the previously chosen reference mesh id across a refresh,
	// where it's still a valid choice.
	const int previousId = _referenceMeshCombo->count() > 0 ? _referenceMeshCombo->currentData().toInt() : -1;

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
		_referenceMeshCombo->addItem(mesh->getName(), QVariant(static_cast<int>(id)));
	}

	if (previousId >= 0)
	{
		const int idx = _referenceMeshCombo->findData(QVariant(previousId));
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
	const int referenceId = _referenceMeshCombo->currentData().toInt();
	if (referenceId < 0 || static_cast<size_t>(referenceId) >= meshStore.size())
		return;
	SceneMesh* referenceMesh = meshStore.at(referenceId);
	if (!referenceMesh || referenceMesh == sampledMesh)
		return;

	const std::vector<float> distances = DeviationAnalyzer::computeDeviation(sampledMesh, referenceMesh);
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
