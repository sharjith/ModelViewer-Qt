#include "SurfaceAnalysisDialog.h"
#include "ModelViewer.h"
#include "ViewportWidget.h"
#include "MeshSelectionBox.h"
#include "NotesListBox.h"
#include "SceneMesh.h"
#include "RenderableMesh.h"
#include "DraftAngleAnalyzer.h"
#include "DeviationAnalyzer.h"
#include "CurvatureAnalyzer.h"
#include "WallThicknessAnalyzer.h"
#include "AnalysisColorRamp.h"
#include "AnalysisMeshSnapshot.h"
#include "AnalysisComputeSession.h"
#include "CoordinateSystemHelper.h"
#include "LengthUnits.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QFrame>
#include <QStyle>
#include <QScreen>
#include <QButtonGroup>
#include <QStackedWidget>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QJsonObject>
#include <QPushButton>
#include <QMessageBox>
#include <QIcon>
#include <QCloseEvent>
#include <QSettings>
#include <QTimer>
#include <QUuid>
#include <QDebug>

#include <algorithm>
#include <any>
#include <cmath>
#include <limits>
#include <utility>
#include <QStringList>
#include <QMdiArea>
#include <QMdiSubWindow>

namespace
{
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
}

SurfaceAnalysisDialog::SurfaceAnalysisDialog(ModelViewer* modelViewer, QWidget* parent)
	: QDialog(parent)
	, _modelViewer(modelViewer)
{
	setWindowTitle(tr("Surface Analysis"));
	resize(420, 420);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(16, 16, 16, 16);
	layout->setSpacing(12);

	auto* introLabel = new QLabel(tr("Analyzes the selected meshes and paints the result directly on the mesh surface."), this);
	introLabel->setWordWrap(true);
	layout->addWidget(introLabel);

	// The meshes this dialog acts on: the shared pick / edit / clear selection box (compact - "3 meshes", never a
	// list of names), seeded from the viewer's selection when the dialog opens. Every Apply reads this list, not the
	// viewer's live selection, so the viewer can be used freely while the dialog stays open. Seeded and connected
	// at the end of the constructor, once every widget exists.
	_selectionBox = new MeshSelectionBox(_modelViewer, this);
	_selectionBox->setFieldToolTip(tr("The meshes to analyze. Right-click to edit or clear."));
	_selectionBox->setEditorTexts(tr("Review and refine the meshes to analyze."), tr("Meshes"));
	layout->addWidget(_selectionBox);

	// Hint line under it ("nothing selected", "Deviation needs exactly one", stale-overlay notice); hidden when empty.
	_selectionStatusLabel = new QLabel(this);
	_selectionStatusLabel->setWordWrap(true);
	_selectionStatusLabel->setVisible(false);
	layout->addWidget(_selectionStatusLabel);
	if (_modelViewer && _modelViewer->getViewportWidget())
	{
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
		button->setIconSize(QSize(48, 48));
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

	// The long explanations live behind a small info button (a click-away popup) instead of taking up
	// dialog space permanently.
	const auto makeInfoButton = [this](QWidget* parent, const QString& text) -> QToolButton*
	{
		auto* button = new QToolButton(parent);
		button->setIcon(style()->standardIcon(QStyle::SP_MessageBoxInformation));
		button->setAutoRaise(true);
		button->setCursor(Qt::PointingHandCursor);
		button->setToolTip(tr("What does this show?"));
		connect(button, &QToolButton::clicked, button, [button, text]()
		{
			auto* popup = new QFrame(button->window(), Qt::Popup);
			popup->setAttribute(Qt::WA_DeleteOnClose);
			popup->setFrameShape(QFrame::StyledPanel);
			auto* popupLayout = new QVBoxLayout(popup);
			auto* label = new QLabel(text, popup);
			label->setWordWrap(true);
			label->setFixedWidth(380);
			popupLayout->addWidget(label);
			popup->adjustSize();
			// Right edge aligned with the button, kept on the screen.
			QPoint pos = button->mapToGlobal(QPoint(button->width() - popup->width(), button->height()));
			if (const QScreen* screen = button->screen())
			{
				const QRect available = screen->availableGeometry();
				pos.setX(std::clamp(pos.x(), available.left(), std::max(available.left(), available.right() - popup->width())));
				pos.setY(std::clamp(pos.y(), available.top(), std::max(available.top(), available.bottom() - popup->height())));
			}
			popup->move(pos);
			popup->show();
		});
		return button;
	};
	const auto makeSectionHeader = [&makeInfoButton](QWidget* parent, const QString& title, const QString& help) -> QHBoxLayout*
	{
		auto* row = new QHBoxLayout();
		auto* titleLabel = new QLabel(title, parent);
		QFont titleFont = titleLabel->font();
		titleFont.setBold(true);
		titleLabel->setFont(titleFont);
		row->addWidget(titleLabel);
		row->addStretch(1);
		row->addWidget(makeInfoButton(parent, help));
		return row;
	};

	// Curvature page - two independent sub-modes sharing one panel: Zebra
	// Stripe (a live view-dependent shader effect, no legend/color scale)
	// and Mean Curvature (a fixed colormap, like Draft Angle/Deviation).
	// Applying one doesn't automatically clear the other's underlying
	// state, but the shader only ever shows one at a time (the colormap
	// overlay takes priority) - Clear Overlay turns both off together.
	{
		auto* page = new QWidget();
		auto* pageLayout = new QVBoxLayout(page);
		pageLayout->addLayout(makeSectionHeader(page, tr("Zebra Stripe"),
			tr("Zebra Stripe reveals surface continuity as a live, view-dependent "
			   "reflection pattern - no legend, since it isn't a fixed color scale.")));
		_zebraStripeToggle = new QPushButton(tr("Zebra Stripe"), page);
		_zebraStripeToggle->setCheckable(true);
		connect(_zebraStripeToggle, &QPushButton::toggled, this, &SurfaceAnalysisDialog::onZebraStripeToggled);
		pageLayout->addWidget(_zebraStripeToggle);

		pageLayout->addSpacing(8);
		pageLayout->addLayout(makeSectionHeader(page, tr("Mean Curvature"),
			tr("Mean Curvature colors each vertex by how sharply the surface "
			   "bends there - blue is concave, red is convex, white is flat. "
			   "Gaussian/principal curvature modes are not yet available. "
			   "Computed on a repaired copy of the mesh (real connectivity is "
			   "required); any repair made is disclosed below after Apply.")));
		_applyCurvatureButton = new QPushButton(tr("Apply Mean Curvature"), page);
		connect(_applyCurvatureButton, &QPushButton::clicked, this, &SurfaceAnalysisDialog::onApplyCurvatureClicked);
		pageLayout->addWidget(_applyCurvatureButton);
		_curvatureRepairNote = new NotesListBox(page); // repairs made per mesh - grouped, height-capped, hidden while empty
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
		pageLayout->addLayout(makeSectionHeader(page, tr("Draft Angle"),
			tr("Draft Angle colors each face by its signed angle to the chosen pull "
			   "direction - red/positive is an ordinary moldable wall, blue/negative "
			   "is an undercut, white is parallel to the pull direction (zero draft).")));

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

		pageLayout->addSpacing(8);
		pageLayout->addLayout(makeSectionHeader(page, tr("Wall-Thickness"),
			tr("Wall-Thickness estimates how thick the material is behind each point of the surface - blue is "
			   "thin, red is thick. Inscribed sphere finds, for each point, the largest sphere that fits inside "
			   "the part while touching the surface there; its diameter is the wall thickness. It reads slanted "
			   "and curved walls correctly, always gives a value, and finds thick spots at corners and rib roots. "
			   "Along a sharp convex edge no large sphere can touch the surface, so a thin border would appear "
			   "there; \"Ignore sharp-edge effect\" (on by default) gives those points the value of the nearest "
			   "interior point of the face instead. Local thickness casts rays into the material from many "
			   "points per face (the ray spread sets how far they may fan out; 0 = straight in only) and Normal "
			   "ray is the older, faster single-ray estimate; both measure along a ray, so they can over-read "
			   "where the far wall is slanted and leave gaps where it is too steep. All methods are ESTIMATES, "
			   "not exact minima. Requires a closed, non-self-intersecting mesh that bounds a volume - otherwise "
			   "the whole mesh is rejected with a reason, never partially colored. Hovering a value writes how "
			   "it was measured to the log.")));

		auto* methodRow = new QHBoxLayout();
		methodRow->addWidget(new QLabel(tr("Method:"), page));
		_thicknessMethodCombo = new QComboBox(page);
		_thicknessMethodCombo->addItem(tr("Inscribed sphere (recommended)"), QVariant(static_cast<int>(WallThicknessMethod::Sphere)));
		_thicknessMethodCombo->addItem(tr("Local thickness (rays)"), QVariant(static_cast<int>(WallThicknessMethod::LocalThickness)));
		_thicknessMethodCombo->addItem(tr("Normal ray (fast)"), QVariant(static_cast<int>(WallThicknessMethod::NormalRay)));
		methodRow->addWidget(_thicknessMethodCombo, 1);
		pageLayout->addLayout(methodRow);

		// Local thickness only: how far off the straight-in direction the rays from each sample may fan out.
		auto* spreadRow = new QHBoxLayout();
		spreadRow->addWidget(new QLabel(tr("Ray spread:"), page));
		_thicknessSpreadSpin = new QDoubleSpinBox(page);
		_thicknessSpreadSpin->setSuffix(QStringLiteral("\u00B0"));
		_thicknessSpreadSpin->setDecimals(0);
		_thicknessSpreadSpin->setRange(0.0, 45.0);
		_thicknessSpreadSpin->setSingleStep(5.0);
		_thicknessSpreadSpin->setValue(0.0);
		_thicknessSpreadSpin->setToolTip(tr("0 measures straight in from each sample (the wall directly behind it). "
		                                    "Larger values also probe obliquely, which finds thin features beside a "
		                                    "sample but reads a flat face's sloped neighbours as thinner."));
		spreadRow->addWidget(_thicknessSpreadSpin, 1);
		pageLayout->addLayout(spreadRow);

		// Inscribed sphere only.
		_thicknessEdgeReliefCheck = new QCheckBox(tr("Ignore sharp-edge effect"), page);
		_thicknessEdgeReliefCheck->setChecked(true);
		_thicknessEdgeReliefCheck->setToolTip(tr("Along a sharp convex edge (or the rim of a hole) the largest sphere that touches "
		                                         "the surface is small, which would read as a thin wall there. With this on, such "
		                                         "points take the value of the nearest interior point of the face. Points squeezed "
		                                         "between two walls (such as the end of a thin rib) keep their small value."));
		pageLayout->addWidget(_thicknessEdgeReliefCheck);
		connect(_thicknessMethodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int)
		{
			const int method = _thicknessMethodCombo->currentData().toInt();
			_thicknessSpreadSpin->setEnabled(method == static_cast<int>(WallThicknessMethod::LocalThickness));
			_thicknessEdgeReliefCheck->setEnabled(method == static_cast<int>(WallThicknessMethod::Sphere));
		});
		_thicknessSpreadSpin->setEnabled(false); // the initial method is Inscribed sphere

		auto* displayRow = new QHBoxLayout();
		displayRow->addWidget(new QLabel(tr("Display:"), page));
		_thicknessDisplayCombo = new QComboBox(page);
		_thicknessDisplayCombo->addItem(tr("Discrete ranges"), true);
		_thicknessDisplayCombo->addItem(tr("Smooth interpolation"), false);
		_thicknessDisplayCombo->setToolTip(tr("Discrete ranges show the calculated result in distinct color bands.\n"
			"Smooth interpolation blends between values and is easier to read, but displayed colors between samples are estimates."));
		displayRow->addWidget(_thicknessDisplayCombo, 1);
		displayRow->addWidget(new QLabel(tr("Bands:"), page));
		_thicknessBandCountSpin = new QSpinBox(page);
		_thicknessBandCountSpin->setRange(3, 20);
		_thicknessBandCountSpin->setValue(10);
		_thicknessBandCountSpin->setToolTip(tr("Number of thickness ranges used by the discrete display."));
		displayRow->addWidget(_thicknessBandCountSpin);
		pageLayout->addLayout(displayRow);
		connect(_thicknessDisplayCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
			this, &SurfaceAnalysisDialog::onThicknessDisplayChanged);
		connect(_thicknessBandCountSpin, QOverload<int>::of(&QSpinBox::valueChanged),
			this, &SurfaceAnalysisDialog::onThicknessDisplayChanged);

		_applyThicknessButton = new QPushButton(tr("Apply Wall-Thickness"), page);
		connect(_applyThicknessButton, &QPushButton::clicked, this, &SurfaceAnalysisDialog::onApplyWallThicknessClicked);
		pageLayout->addWidget(_applyThicknessButton);

		// Pass/fail display: on a chunky machined part a continuous ramp mostly shows the part's size, while
		// "is anything thinner than my limit" is the question that matters. Re-colours the existing result.
		auto* limitRow = new QHBoxLayout();
		_thicknessHighlightCheck = new QCheckBox(tr("Highlight walls thinner than:"), page);
		limitRow->addWidget(_thicknessHighlightCheck);
		_thicknessLimitSpin = new QDoubleSpinBox(page);
		_thicknessLimitSpin->setSuffix(tr(" mm"));
		_thicknessLimitSpin->setDecimals(2);
		_thicknessLimitSpin->setRange(0.01, 100000.0);
		_thicknessLimitSpin->setSingleStep(0.5);
		_thicknessLimitSpin->setValue(1.0);
		limitRow->addWidget(_thicknessLimitSpin, 1);
		pageLayout->addLayout(limitRow);
		connect(_thicknessHighlightCheck, &QCheckBox::toggled, this, &SurfaceAnalysisDialog::onThicknessDisplayChanged);
		connect(_thicknessLimitSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SurfaceAnalysisDialog::onThicknessDisplayChanged);

		_thicknessSummaryLabel = new QLabel(page);
		_thicknessSummaryLabel->setWordWrap(true);
		_thicknessSummaryLabel->setVisible(false);
		pageLayout->addWidget(_thicknessSummaryLabel);

		_thicknessRejectionNote = new NotesListBox(page); // why a mesh was rejected - grouped, height-capped, hidden while empty
		pageLayout->addWidget(_thicknessRejectionNote);

		pageLayout->addStretch(1);
		_stack->addWidget(page);
	}

	// Deviation page - unsigned nearest-surface distance from the selected
	// (scan/comparison) mesh to a chosen reference mesh.
	{
		auto* page = new QWidget();
		auto* pageLayout = new QVBoxLayout(page);
		pageLayout->addLayout(makeSectionHeader(page, tr("Deviation"),
			tr("Colors the selected mesh by its unsigned distance to a reference mesh's "
			   "surface - dark blue is a close match, red is the largest deviation found. "
			   "Both meshes must already be aligned in the same coordinate frame and use "
			   "the same units; a plain offset between them will read as a false deviation. "
			   "This is a sampled result (measured per vertex), not exhaustive coverage.")));

		// The reference mesh: the same selection control as the meshes above, in its one-mesh form (a combo of every
		// mesh in the scene does not scale to a large assembly).
		_referenceBox = new MeshSelectionBox(_modelViewer, page);
		_referenceBox->setLabelText(tr("Reference mesh:"));
		_referenceBox->setSingleMeshMode(true);
		_referenceBox->setFieldToolTip(tr("The reference mesh the selected mesh is compared against. Right-click to clear."));
		connect(_referenceBox, &MeshSelectionBox::excludedMeshesRejected, this, [this](int) { onReferenceMeshRejected(); });
		pageLayout->addWidget(_referenceBox);

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

	// Shared across all 3 pages, same as _legendLabel above - the readout
	// itself is mode-agnostic (see hoverReadoutText()). Default on: a
	// numeric readout under the cursor is directly useful the first time
	// someone applies an overlay, not something that needs discovering.
	_hoverReadoutToggle = new QPushButton(tr("Show Readout on Hover"), this);
	_hoverReadoutToggle->setCheckable(true);
	_hoverReadoutToggle->setChecked(true);
	layout->addWidget(_hoverReadoutToggle);
	// hoverReadoutEnabled() is only ever re-checked lazily, on the next
	// passive mouse move (updateSurfaceAnalysisHoverReadout()) - without
	// this, unchecking the toggle while the pointer sits still left the old
	// readout label on screen until the mouse happened to move again.
	connect(_hoverReadoutToggle, &QPushButton::toggled, this, [this](bool checked) {
		if (!checked)
		{
			if (ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr)
				viewport->clearSurfaceAnalysisHoverReadout();
		}
	});

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

	// Seed the mesh list from the viewer's selection (before connecting, so it does not trigger a refresh), then
	// keep the hint and the Deviation reference choices in step with the list.
	_selectionBox->seedFromViewportSelection();
	connect(_selectionBox, &MeshSelectionBox::meshUuidsChanged, this, &SurfaceAnalysisDialog::onSelectionChanged);
	syncReferenceExclusions();

	_curvatureButton->setChecked(true);
	onModeChanged();

	// See checkForStaleOverlays()'s own doc comment - runs continuously
	// rather than being started/stopped around visibility/overlay-count,
	// since the slot itself already early-outs cheaply when there's nothing
	// to do.
	_stalenessTimer = new QTimer(this);
	_stalenessTimer->setInterval(500);
	connect(_stalenessTimer, &QTimer::timeout, this, &SurfaceAnalysisDialog::checkForStaleOverlays);
	_stalenessTimer->start();

	// Hide/show this dialog as its OWN document's MDI subwindow loses/gains focus - mirrors RepairMeshDialog/
	// ShrinkWrapDialog/FillHolesDialog's identical mechanism. Without this, a dialog opened for one document
	// kept showing (and still acting on) that document's stale selection even while a different one became the
	// active tab.
	if (_modelViewer)
	{
		connect(_modelViewer, &ModelViewer::importUnitsChanged,
			this, &SurfaceAnalysisDialog::onImportUnitsChanged);
		if (QMdiArea* mdiArea = findMdiArea(_modelViewer))
			connect(mdiArea, &QMdiArea::subWindowActivated, this, &SurfaceAnalysisDialog::onActiveSubWindowChanged);
	}

	loadSettings();
}

void SurfaceAnalysisDialog::onActiveSubWindowChanged(QMdiSubWindow* activeSubWindow)
{
	const bool isOwnDocumentActive = _modelViewer
		&& activeSubWindow
		&& activeSubWindow->widget() == static_cast<QWidget*>(_modelViewer);
	setVisible(isOwnDocumentActive);
}

void SurfaceAnalysisDialog::closeEvent(QCloseEvent* event)
{
	// The dialog has WA_DeleteOnClose - actually letting this close while an
	// AnalysisComputeSession::runBlocking() call is still on the stack
	// somewhere below this event (reachable because that call runs its own
	// nested QEventLoop, which is what let this close attempt be processed
	// at all) would destroy `this` out from under that still-running frame,
	// a use-after-free once it eventually returns. Redirect to Cancel and
	// refuse to close instead - the close attempt can be repeated once the
	// cancellation actually completes and unwinds.
	if (_activeSession)
	{
		_activeSession->requestCancel();
		event->ignore();
		return;
	}

	// See this class's header doc comment - closing (via Close, the window's
	// X button, or any other path) always clears everything this dialog
	// applied, since WA_DeleteOnClose means there's no "Clear Overlay"
	// button left to press afterward.
	clearAllOverlays();
	saveSettings();
	QDialog::closeEvent(event);
}

void SurfaceAnalysisDialog::reject()
{
	// Same reasoning as closeEvent() above - Escape doesn't route through
	// closeEvent() (QDialog::reject() only hide()s), so this needs the
	// identical in-flight guard independently, not just the double-override
	// pattern MeasurementDialog already uses for the unrelated "don't skip
	// cleanup" reason.
	if (_activeSession)
	{
		_activeSession->requestCancel();
		return;
	}

	clearAllOverlays();
	saveSettings();
	QDialog::reject();
}

void SurfaceAnalysisDialog::setComputationInFlight(bool inFlight, QPushButton* activeButton, const QString& buttonText)
{
	if (activeButton)
		activeButton->setText(buttonText);

	const bool enabled = !inFlight;
	if (_curvatureButton) _curvatureButton->setEnabled(enabled);
	if (_thicknessButton) _thicknessButton->setEnabled(enabled);
	if (_deviationButton) _deviationButton->setEnabled(enabled);
	if (_pullDirectionCombo) _pullDirectionCombo->setEnabled(enabled);
	if (_referenceBox) _referenceBox->setEnabled(enabled);
	if (_zebraStripeToggle) _zebraStripeToggle->setEnabled(enabled);
	if (_clearButton) _clearButton->setEnabled(enabled);
	if (_selectionBox) _selectionBox->setEnabled(enabled);
	for (QPushButton* button : { _applyCurvatureButton, _applyDraftButton, _applyThicknessButton, _applyDeviationButton })
	{
		if (button && button != activeButton)
			button->setEnabled(enabled);
	}
}

void SurfaceAnalysisDialog::loadSettings()
{
	QSettings settings;
	const QByteArray geometry = settings.value("surfaceAnalysis/geometry", QByteArray()).toByteArray();
	if (!geometry.isEmpty())
		restoreGeometry(geometry);
}

void SurfaceAnalysisDialog::saveSettings()
{
	QSettings settings;
	settings.setValue("surfaceAnalysis/geometry", saveGeometry());
}

SurfaceAnalysisDialog::Mode SurfaceAnalysisDialog::currentMode() const
{
	if (_thicknessButton->isChecked())
		return Mode::WallThickness;
	if (_deviationButton->isChecked())
		return Mode::Deviation;
	return Mode::Curvature;
}

bool SurfaceAnalysisDialog::hoverReadoutEnabled() const
{
	return _hoverReadoutToggle && _hoverReadoutToggle->isChecked();
}

QString SurfaceAnalysisDialog::hoverReadoutText(const MeshSurfaceAnchor& anchor, QColor& outTextColor) const
{
	if (!anchor.isValid() || !_modelViewer || !_modelViewer->getViewportWidget())
		return QString();

	SceneMesh* mesh = _modelViewer->getViewportWidget()->getMeshByUuid(anchor.meshUuid);
	if (!mesh)
		return QString();

	float value = 0.0f;
	AnalysisKind kind = AnalysisKind::Curvature;
	if (!_overlay.scalarAt(mesh, anchor.triangleIndex, anchor.barycentric, value, kind))
	{
		// No value here. For wall thickness say why (the gap is a measurement outcome, not a rendering fault).
		AnalysisKind overlayKind;
		if (_overlay.kindOf(mesh, overlayKind) && overlayKind == AnalysisKind::WallThickness)
		{
			const QString text = thicknessNoValueText(mesh, anchor);
			if (!text.isEmpty())
				outTextColor = Qt::white;
			return text;
		}
		return QString();
	}
	if (kind == AnalysisKind::WallThickness)
		logThicknessWitness(mesh, anchor, value);

	// Same lightness() < 128 -> white / else black convention this app's
	// own hatch-line-color picker already used (see the now-removed
	// on_pushButtonHatchColor_clicked() this was lifted from) - picks
	// whichever of black/white actually reads clearly against the exact
	// heatmap color at this point, not a fixed color that goes invisible
	// over the ramp's lighter bands. Falls back to white (this function's
	// existing default before outTextColor existed) if colorAt() somehow
	// fails right after scalarAt() just succeeded - shouldn't happen since
	// both resolve the same entry, but a readout with a slightly-wrong
	// color is still better than none.
	QColor heatmapColor;
	if (_overlay.colorAt(mesh, anchor.triangleIndex, anchor.barycentric, heatmapColor))
		outTextColor = (heatmapColor.lightness() < 128) ? Qt::white : Qt::black;
	else
		outTextColor = Qt::white;

	// The label comes from the analysis kind the overlay itself recorded - NOT from how the result was uploaded.
	// (Draft Angle and Wall-Thickness are both per-face, so "per-face" used to mean "Draft" and a thickness value
	// was shown as degrees.) Wall thickness is stored in millimetres (see applyWallThicknessToSelection()), which
	// the legend states too; the other distance-like modes keep no unit because this app has no fixed length-unit
	// convention across imported models.
	switch (kind)
	{
	case AnalysisKind::DraftAngle:
		return tr("Draft: %1°").arg(value, 0, 'f', 2);
	case AnalysisKind::Curvature:
		return tr("Curvature: %1").arg(value, 0, 'f', 4);
	case AnalysisKind::WallThickness:
		return tr("Thickness: %1 mm").arg(value, 0, 'f', 3);
	case AnalysisKind::Deviation:
		return tr("Deviation: %1").arg(value, 0, 'f', 3);
	}
	return QString();
}

const WallThicknessWitness* SurfaceAnalysisDialog::thicknessWitnessAt(SceneMesh* mesh, const MeshSurfaceAnchor& anchor,
                                                                      int& outGrid, int& outSample, float& outToMm) const
{
	const auto it = _thicknessWitness.constFind(mesh);
	if (it == _thicknessWitness.constEnd() || anchor.triangleIndex < 0)
		return nullptr;
	const ThicknessWitnessSet& set = it.value();
	const size_t triangle = static_cast<size_t>(anchor.triangleIndex);
	if (triangle >= set.gridN.size() || triangle >= set.offset.size() || set.gridN[triangle] == 0)
		return nullptr;
	outGrid = set.gridN[triangle];
	outSample = SubTriangleGrid::indexAt(outGrid, anchor.barycentric.y(), anchor.barycentric.z());
	const size_t index = static_cast<size_t>(set.offset[triangle]) + static_cast<size_t>(outSample);
	if (index >= set.witness.size())
		return nullptr;
	outToMm = set.toMm;
	return &set.witness[index];
}

QString SurfaceAnalysisDialog::thicknessStatusText(WallThicknessSampleStatus status)
{
	switch (status)
	{
	case WallThicknessSampleStatus::NoHit:
		return tr("no wall found behind the surface");
	case WallThicknessSampleStatus::OtherSolid:
		return tr("the wall behind belongs to a different body");
	case WallThicknessSampleStatus::EnteringFace:
		return tr("the ray meets a surface from outside (touching or overlapping bodies)");
	case WallThicknessSampleStatus::GlancingExit:
		return tr("the wall behind is too steep to measure straight through");
	case WallThicknessSampleStatus::DegenerateHit:
		return tr("the wall behind is a degenerate triangle");
	case WallThicknessSampleStatus::Valid:
		break;
	}
	return QString();
}

QString SurfaceAnalysisDialog::thicknessNoValueText(SceneMesh* mesh, const MeshSurfaceAnchor& anchor) const
{
	int grid = 0, sample = 0;
	float toMm = 1.0f;
	const WallThicknessWitness* w = thicknessWitnessAt(mesh, anchor, grid, sample, toMm);
	if (!w || w->status == WallThicknessSampleStatus::Valid)
		return QString();
	logThicknessWitness(mesh, anchor, std::numeric_limits<float>::quiet_NaN());
	return tr("No value: %1").arg(thicknessStatusText(w->status));
}

void SurfaceAnalysisDialog::logThicknessWitness(SceneMesh* mesh, const MeshSurfaceAnchor& anchor, float valueMm) const
{
	// Writes the ray behind the hovered sub-triangle's value to the log - once per sample, not once per mouse move -
	// so a value that looks wrong can be checked against the geometry: where the ray started, where it left the
	// material, how far it went and at what angle. For a sample without a value (valueMm is NaN) it logs the axis ray
	// and why it was ruled out.
	int n = 0, sample = 0;
	float toMm = 1.0f;
	const WallThicknessWitness* witness = thicknessWitnessAt(mesh, anchor, n, sample, toMm);
	if (!witness)
		return;
	const auto setIt = _thicknessWitness.constFind(mesh);
	const bool sphere = setIt != _thicknessWitness.constEnd() && setIt->sphere;
	if (mesh == _lastLoggedThicknessMesh && anchor.triangleIndex == _lastLoggedThicknessTriangle && sample == _lastLoggedThicknessSample)
		return;
	_lastLoggedThicknessMesh = mesh;
	_lastLoggedThicknessTriangle = anchor.triangleIndex;
	_lastLoggedThicknessSample = sample;

	const WallThicknessWitness& w = *witness;
	const double lengthMm = static_cast<double>(w.distance) * toMm;
	const QString origin = QStringLiteral("(%1, %2, %3)").arg(w.origin[0], 0, 'g', 7).arg(w.origin[1], 0, 'g', 7).arg(w.origin[2], 0, 'g', 7);
	const QString hitPoint = w.hitTriangle >= 0
		? QStringLiteral("(%1, %2, %3)").arg(w.hit[0], 0, 'g', 7).arg(w.hit[1], 0, 'g', 7).arg(w.hit[2], 0, 'g', 7)
		: QStringLiteral("none");

	if (std::isnan(valueMm))
	{
		qInfo().noquote() << QStringLiteral("[WallThickness] '%1' triangle %2 (%3x%3 samples), sample %4: NO VALUE - %5 | axis ray from %6, "
			"last wall met: point %7 at %8 mm, triangle %9, facing %10 | points in model units, lengths in mm")
			.arg(mesh->getName()).arg(anchor.triangleIndex).arg(n).arg(sample)
			.arg(thicknessStatusText(w.status)).arg(origin).arg(hitPoint).arg(lengthMm, 0, 'f', 3)
			.arg(w.hitTriangle).arg(w.facing, 0, 'f', 3);
		return;
	}

	if (sphere)
	{
		// The witness is the contact that limits the sphere: it touches the surface at `origin` and here.
		qInfo().noquote() << QStringLiteral("[WallThickness] '%1' triangle %2 (%3x%3 samples), sample %4: %5 mm | inscribed sphere, diameter %6 mm, "
			"touching the surface at %7 and at %8 (triangle %9, wall facing %10), contact %11 deg off the inward normal "
			"| points in model units, lengths in mm")
			.arg(mesh->getName()).arg(anchor.triangleIndex).arg(n).arg(sample)
			.arg(valueMm, 0, 'f', 3).arg(lengthMm, 0, 'f', 3).arg(origin).arg(hitPoint)
			.arg(w.hitTriangle).arg(w.facing, 0, 'f', 3).arg(w.angleDegrees, 0, 'f', 1);
		if (w.reliefSteps > 0)
		{
			// The sphere above was measured at an interior point, not at the sample: the sample sat within reach of a
			// sharp convex edge, which limits a sphere touching the surface there.
			qInfo().noquote() << QStringLiteral("[WallThickness]   edge relief: value taken from the interior point (%1, %2, %3) after %4 move(s) off the edge")
				.arg(w.source[0], 0, 'g', 7).arg(w.source[1], 0, 'g', 7).arg(w.source[2], 0, 'g', 7).arg(static_cast<int>(w.reliefSteps));
		}
		return;
	}

	qInfo().noquote() << QStringLiteral("[WallThickness] '%1' triangle %2 (%3x%3 samples), sample %4: %5 mm | ray angle %6 deg, "
		"length %7 mm, from %8 to %9, leaves through triangle %10 (facing %11) | length / cos(angle) = %12 mm "
		"| points in model units, lengths in mm")
		.arg(mesh->getName()).arg(anchor.triangleIndex).arg(n).arg(sample)
		.arg(valueMm, 0, 'f', 3).arg(w.angleDegrees, 0, 'f', 1).arg(lengthMm, 0, 'f', 3)
		.arg(origin).arg(hitPoint).arg(w.hitTriangle).arg(w.facing, 0, 'f', 3)
		.arg(lengthMm / std::max(1.0e-6, std::cos(static_cast<double>(w.angleDegrees) * 3.14159265358979323846 / 180.0)), 0, 'f', 3);
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
		_curvatureRepairNote->clearNotes();
	if (_thicknessRejectionNote)
		_thicknessRejectionNote->clearNotes();
	if (_thicknessSummaryLabel)
		_thicknessSummaryLabel->setVisible(false);

	updateSelectionStatusLabel();
}

void SurfaceAnalysisDialog::onSelectionChanged()
{
	// The Deviation reference picker must not offer the meshes being analyzed - that list just changed.
	syncReferenceExclusions();

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
	_thicknessWitness.remove(mesh);
	if (_lastLoggedThicknessMesh == mesh)
		_lastLoggedThicknessMesh = nullptr;
	_zebraStripeMeshes.remove(mesh);

	// A background AnalysisComputeSession never dereferences a snapshot's
	// meshHandle - see AnalysisMeshSnapshot's own doc comment - but the
	// CALLER (this dialog) does, once the session returns, to re-validate
	// and apply a result. Record this mesh so that later code skips it
	// entirely rather than dereferencing a pointer that's dangling by then.
	if (_activeSession)
		_deletedWhileComputing.insert(mesh);
}

void SurfaceAnalysisDialog::requestComputationCancel()
{
	if (_activeSession)
		_activeSession->requestCancel();
}

void SurfaceAnalysisDialog::onImportUnitsChanged()
{
	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	if (!viewport)
		return;

	bool cleared = false;
	viewport->makeCurrent();
	for (SceneMesh* mesh : _overlay.trackedMeshes())
	{
		AnalysisKind kind;
		if (!_overlay.kindOf(mesh, kind) || kind != AnalysisKind::WallThickness)
			continue;
		_overlay.clearOverlay(mesh);
		_thicknessWitness.remove(mesh);
		cleared = true;
	}
	viewport->doneCurrent();
	if (!cleared)
		return;

	_lastLoggedThicknessMesh = nullptr;
	_legendLabel->setVisible(false);
	if (_thicknessSummaryLabel)
		_thicknessSummaryLabel->setVisible(false);
	viewport->clearSurfaceAnalysisHoverReadout();
	viewport->update();
	if (_selectionStatusLabel)
	{
		_selectionStatusLabel->setText(tr("Wall-Thickness overlay cleared because the import units changed. Click Apply to recompute."));
		_selectionStatusLabel->setVisible(true);
	}
}

void SurfaceAnalysisDialog::checkForStaleOverlays()
{
	// Per-document singleton - a background/inactive document's dialog
	// still exists (findChild-reused) but has no reason to spend even a
	// cheap check every 500ms while nobody can see its overlays anyway.
	if (!isVisible())
		return;

	// Ticks on every transform AND geometry change already (see
	// RenderableMesh::currentRuntimeBoundsRevision()'s own doc comment) -
	// comparing against the last value seen here is a cheap way to skip all
	// the real work below on every tick where genuinely nothing happened,
	// the same poll-a-monotonic-revision idiom
	// SceneRuntime::refreshRuntimeVisibilityCacheForCurrentView() already
	// uses for the identical reason.
	const quint64 currentRevision = RenderableMesh::currentRuntimeBoundsRevision();
	if (currentRevision == _lastSeenBoundsRevision)
		return;
	_lastSeenBoundsRevision = currentRevision;

	const QList<SceneMesh*> tracked = _overlay.trackedMeshes();
	if (tracked.isEmpty())
		return;

	int staleCount = 0;
	for (SceneMesh* mesh : tracked)
	{
		if (!mesh)
			continue;

		// Re-derive computeCurrentKey()'s parameters/referenceMesh from
		// this mesh's own already-stored key - isValid() alone can't
		// reconstruct the analysis-mode-specific parts, and this dialog has
		// no reason to keep a second, parallel copy of them itself when
		// _overlay already holds the authoritative one.
		const SurfaceAnalysisOverlay::CacheKey stored = _overlay.storedKey(mesh);
		const SurfaceAnalysisOverlay::CacheKey current =
			SurfaceAnalysisOverlay::computeCurrentKey(mesh, stored.parameters, stored.referenceMesh);
		if (_overlay.isValid(mesh, current))
			continue;

		// Auto-clear, not "gray out with a re-Apply prompt" - see
		// SurfaceAnalysisOverlay's own doc comment on why: no rendering path
		// supports a grayed-out-but-shown overlay state, and an explicit,
		// disclosed absence beats an ambiguous stale-looking display. Zebra
		// Stripe is deliberately left untouched here - it's a live, view-
		// dependent shader effect re-evaluated every frame from the mesh's
		// CURRENT geometric normals, not a cached result computed against a
		// point-in-time snapshot, so a transform change can never make it
		// stale in the first place (unlike this loop's _overlay-tracked
		// colormap results).
		_overlay.clearOverlay(mesh);
		++staleCount;
	}

	if (staleCount == 0)
		return;

	if (ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr)
	{
		// A stale overlay this loop just auto-cleared may be the one the
		// cached hover-readout text was computed from - same reasoning as
		// clearSelectionOverlays()/clearAllOverlays(), just reached from this
		// periodic check instead of a button/close event.
		viewport->clearSurfaceAnalysisHoverReadout();
		viewport->update();
	}

	if (_selectionStatusLabel)
	{
		_selectionStatusLabel->setText(tr("Overlay cleared for %1 mesh(es) - transform changed, click Apply to recompute.").arg(staleCount));
		_selectionStatusLabel->setVisible(true);
	}
}

void SurfaceAnalysisDialog::seedFromViewportSelection()
{
	if (!_activeSession && _selectionBox)
		_selectionBox->seedFromViewportSelection(); // refreshes through onSelectionChanged()
}

std::vector<int> SurfaceAnalysisDialog::selectedMeshIds() const
{
	return _selectionBox ? _selectionBox->meshIds() : std::vector<int>();
}

void SurfaceAnalysisDialog::updateSelectionStatusLabel()
{
	if (!_selectionStatusLabel)
		return;

	const size_t count = selectedMeshIds().size();
	QString text;
	if (count == 0)
		text = tr("No mesh selected - add meshes with the selection box above.");
	else if (currentMode() == Mode::Deviation && count != 1)
		text = tr("%1 meshes selected - Deviation needs exactly one (the scan/comparison side).").arg(static_cast<int>(count));
	_selectionStatusLabel->setText(text);
	_selectionStatusLabel->setVisible(!text.isEmpty());
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
	// The button is repurposed into Cancel while a computation is already in
	// flight (see setComputationInFlight()'s own doc comment) - a re-click in
	// that state means "cancel", not "start a second overlapping run".
	if (_activeSession)
	{
		_activeSession->requestCancel();
		return;
	}
	applyDraftAngleToSelection();
}

void SurfaceAnalysisDialog::onApplyDeviationClicked()
{
	if (_activeSession)
	{
		_activeSession->requestCancel();
		return;
	}
	applyDeviationToSelection();
}

void SurfaceAnalysisDialog::onApplyCurvatureClicked()
{
	if (_activeSession)
	{
		_activeSession->requestCancel();
		return;
	}
	applyCurvatureToSelection();
}

void SurfaceAnalysisDialog::onApplyWallThicknessClicked()
{
	if (_activeSession)
	{
		_activeSession->requestCancel();
		return;
	}
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

	const std::vector<int> selected = selectedMeshIds();
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

	const std::vector<int> selected = selectedMeshIds();
	if (selected.empty())
	{
		QMessageBox::information(this, tr("Surface Analysis"), tr("Select one or more meshes first."));
		return;
	}

	const std::vector<SceneMesh*> meshStore = viewport->getMeshStore();

	QVariantMap params;
	params.insert(QStringLiteral("mode"), QStringLiteral("meanCurvature"));

	std::vector<AnalysisMeshSnapshot> snapshots;
	snapshots.reserve(selected.size());
	for (int id : selected)
		snapshots.push_back(captureAnalysisMeshSnapshot(meshStore.at(id), params));

	_deletedWhileComputing.clear();
	const QString originalText = _applyCurvatureButton->text();
	setComputationInFlight(true, _applyCurvatureButton, tr("Cancel"));

	AnalysisComputeSession session(this);
	_activeSession = &session;

	// CurvatureAnalyzer's repair + interpolated-corrected-curvatures +
	// per-vertex AABB locate is the heaviest of this dialog's analyzers -
	// the whole reason this dialog needed a real background worker, not
	// just a wait cursor.
	const std::vector<AnalysisComputeSession::PerMeshOutcome> outcomes = session.runBlocking(
		std::move(snapshots),
		[](const AnalysisMeshSnapshot& snapshot, const std::atomic<bool>& cancelRequested) -> std::any
		{
			try
			{
				return CurvatureAnalyzer::computeMeanCurvature(
					snapshot.points, snapshot.normals, snapshot.indices, -1.0, &cancelRequested);
			}
			catch (...)
			{
				// A malformed mesh can make a third-party repair/curvature
				// routine throw. Return a normal failed result so the UI clears
				// any stale overlay and reports the failure instead of allowing
				// an exception to escape the worker thread.
				return CurvatureResult{};
			}
		});

	_activeSession = nullptr;
	setComputationInFlight(false, _applyCurvatureButton, originalText);

	if (outcomes.empty())
		return; // cancelled - AnalysisComputeSession never half-applies a batch

	struct PerMesh { SceneMesh* mesh; CurvatureResult result; SurfaceAnalysisOverlay::CacheKey key; };
	std::vector<PerMesh> perMesh;
	perMesh.reserve(outcomes.size());

	std::vector<float> curvatureMagnitudes;
	bool anyValid = false;
	bool anyStale = false;
	QVector<NotesListBox::Note> repairNotes;

	for (const AnalysisComputeSession::PerMeshOutcome& outcome : outcomes)
	{
		SceneMesh* mesh = outcome.meshHandle;
		if (!mesh || _deletedWhileComputing.contains(mesh))
		{
			anyStale = true;
			continue; // deleted while this ran - see _deletedWhileComputing's own doc comment
		}

		// Re-validate against the mesh's CURRENT state before trusting a
		// result computed on a background thread. See
		// SurfaceAnalysisOverlay::computeCurrentKey()'s own doc comment.
		const SurfaceAnalysisOverlay::CacheKey currentKey = SurfaceAnalysisOverlay::computeCurrentKey(mesh, params);
		if (!(currentKey == outcome.snapshotKey))
		{
			anyStale = true;
			continue; // stale - mesh changed mid-computation, discard rather than apply
		}

		const CurvatureResult* result = std::any_cast<CurvatureResult>(&outcome.result);
		if (!result)
			continue;

		if (result->succeeded)
		{
			repairNotes.append({ mesh->getName(), result->repairSummary, NotesListBox::Severity::Info });
			for (size_t i = 0; i < result->meanCurvaturePerVertex.size() && i < result->validPerVertex.size(); ++i)
			{
				if (result->validPerVertex[i] && std::isfinite(result->meanCurvaturePerVertex[i]))
				{
					curvatureMagnitudes.push_back(std::fabs(result->meanCurvaturePerVertex[i]));
					anyValid = true;
				}
			}
		}
		perMesh.push_back({ mesh, *result, outcome.snapshotKey });
	}

	if (perMesh.empty())
	{
		if (anyStale)
		{
			QMessageBox::information(this, tr("Surface Analysis"),
				tr("Selection changed during computation - re-run Apply."));
		}
		return;
	}

	float bound = curvatureMagnitudes.empty()
		? 0.0f : *std::max_element(curvatureMagnitudes.begin(), curvatureMagnitudes.end());
	if (curvatureMagnitudes.size() >= 16)
	{
		const size_t rank = std::min(curvatureMagnitudes.size() - 1,
			static_cast<size_t>(std::ceil(curvatureMagnitudes.size() * 0.98)) - 1);
		std::nth_element(curvatureMagnitudes.begin(), curvatureMagnitudes.begin() + rank, curvatureMagnitudes.end());
		bound = curvatureMagnitudes[rank];
	}
	const float rangeMin = bound > 1.0e-6f ? -bound : -1.0f;
	const float rangeMax = bound > 1.0e-6f ? bound : 1.0f;

	// setAnalysisOverlayColors() below uploads a real GPU buffer - same
	// makeCurrent()/doneCurrent() reasoning as every other Apply here. Runs
	// for EVERY surviving mesh regardless of anyValid below (including the
	// all-failed case) - a mesh whose curvature computation failed must have
	// whatever UNRELATED overlay it happened to already be showing (e.g. a
	// Draft Angle result from an earlier Apply) cleared too, not left
	// silently displayed under this run's new (curvature-scaled) legend.
	viewport->makeCurrent();
	for (PerMesh& pm : perMesh)
	{
		if (!pm.result.succeeded)
		{
			_overlay.clearOverlay(pm.mesh);
			continue;
		}
		_overlay.applyResult(pm.mesh, pm.result.meanCurvaturePerVertex, pm.result.validPerVertex,
			pm.key, rangeMin, rangeMax, AnalysisColormap::Diverging, AnalysisKind::Curvature);
	}
	viewport->doneCurrent();
	viewport->update();

	if (!anyValid)
	{
		QMessageBox::warning(this, tr("Surface Analysis"),
			tr("Could not compute a usable curvature result for the current selection."));
		return;
	}

	_legendLabel->setPixmap(AnalysisColorRamp::legendGradient(
		280, 44, rangeMin, rangeMax, AnalysisColormap::Diverging, QString(),
		curvatureMagnitudes.size() >= 16, 0, curvatureMagnitudes.size() >= 16));
	_legendLabel->setVisible(true);

	if (_curvatureRepairNote)
		_curvatureRepairNote->setNotes(repairNotes);
}

void SurfaceAnalysisDialog::applyWallThicknessToSelection()
{
	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	if (!viewport)
		return;

	const std::vector<int> selected = selectedMeshIds();
	if (selected.empty())
	{
		QMessageBox::information(this, tr("Surface Analysis"), tr("Select one or more meshes first."));
		return;
	}

	const std::vector<SceneMesh*> meshStore = viewport->getMeshStore();

	QVariantMap params;
	params.insert(QStringLiteral("mode"), QStringLiteral("wallThickness"));
	// The method is part of the cache key: switching it must not leave a result computed the other way looking valid.
	WallThicknessParams analysisParams;
	analysisParams.method = _thicknessMethodCombo
		? static_cast<WallThicknessMethod>(_thicknessMethodCombo->currentData().toInt())
		: WallThicknessMethod::Sphere;
	params.insert(QStringLiteral("method"), static_cast<int>(analysisParams.method));
	analysisParams.coneHalfAngleDegrees = _thicknessSpreadSpin ? _thicknessSpreadSpin->value() : 0.0;
	if (analysisParams.method == WallThicknessMethod::LocalThickness)
		params.insert(QStringLiteral("spreadDegrees"), analysisParams.coneHalfAngleDegrees);
	analysisParams.edgeRelief = !_thicknessEdgeReliefCheck || _thicknessEdgeReliefCheck->isChecked();
	if (analysisParams.method == WallThicknessMethod::Sphere)
		params.insert(QStringLiteral("edgeRelief"), analysisParams.edgeRelief);

	std::vector<AnalysisMeshSnapshot> snapshots;
	snapshots.reserve(selected.size());
	for (int id : selected)
	{
		SceneMesh* mesh = meshStore.at(id);
		QVariantMap meshParams = params;
		meshParams.insert(QStringLiteral("millimetersPerUnit"), lengthScaleForMesh(mesh));
		snapshots.push_back(captureAnalysisMeshSnapshot(mesh, meshParams));
	}

	_deletedWhileComputing.clear();
	const QString originalText = _applyThicknessButton->text();
	setComputationInFlight(true, _applyThicknessButton, tr("Cancel"));

	AnalysisComputeSession session(this);
	_activeSession = &session;

	// This is the heaviest of this dialog's four analyses (whole-mesh
	// topology validation, orientation resolution, solid-region
	// classification, then a full AABB-tree multi-hit ray per face) - the
	// other reason (besides Curvature) this dialog needed a real background
	// worker, not just a wait cursor.
	const std::vector<AnalysisComputeSession::PerMeshOutcome> outcomes = session.runBlocking(
		std::move(snapshots),
		[analysisParams](const AnalysisMeshSnapshot& snapshot, const std::atomic<bool>& cancelRequested) -> std::any
		{
			return WallThicknessAnalyzer::computeThickness(snapshot.points, snapshot.indices, analysisParams, &cancelRequested);
		});

	_activeSession = nullptr;
	setComputationInFlight(false, _applyThicknessButton, originalText);

	if (outcomes.empty())
		return; // cancelled - AnalysisComputeSession never half-applies a batch

	struct PerMesh { SceneMesh* mesh; WallThicknessResult result; SurfaceAnalysisOverlay::CacheKey key; };
	std::vector<PerMesh> perMesh;
	perMesh.reserve(outcomes.size());

	float maxThickness = 0.0f;
	std::vector<float> pooledThickness; // every valid value, in mm, across all meshes - for the robust range
	bool anyValid = false;
	bool anyStale = false;
	QVector<NotesListBox::Note> rejectionNotes;

	for (const AnalysisComputeSession::PerMeshOutcome& outcome : outcomes)
	{
		SceneMesh* mesh = outcome.meshHandle;
		if (!mesh || _deletedWhileComputing.contains(mesh))
		{
			anyStale = true;
			continue; // deleted while this ran - see _deletedWhileComputing's own doc comment
		}

		// Re-validate against the mesh's CURRENT state before trusting a
		// result computed on a background thread. See
		// SurfaceAnalysisOverlay::computeCurrentKey()'s own doc comment.
		QVariantMap currentParams = outcome.snapshotKey.parameters;
		currentParams.insert(QStringLiteral("millimetersPerUnit"), lengthScaleForMesh(mesh));
		const SurfaceAnalysisOverlay::CacheKey currentKey = SurfaceAnalysisOverlay::computeCurrentKey(mesh, currentParams);
		if (!(currentKey == outcome.snapshotKey))
		{
			anyStale = true;
			continue; // stale - mesh changed mid-computation, discard rather than apply
		}

		const WallThicknessResult* result = std::any_cast<WallThicknessResult>(&outcome.result);
		if (!result)
			continue;

		WallThicknessResult scaled = *result;
		if (scaled.succeeded)
		{
			// The analyzer works in the mesh's own coordinate units; convert to millimetres once, here, so the
			// overlay's stored values, the legend and the hover readout all speak the same unit.
			const float toMm = static_cast<float>(lengthScaleForMesh(mesh));
			for (size_t i = 0; i < scaled.thicknessPerFace.size(); ++i)
			{
				scaled.thicknessPerFace[i] *= toMm;
				if (scaled.validPerFace[i])
					anyValid = true;
			}
			for (float& v : scaled.samples.values)
				v *= toMm; // NaN (no value) stays NaN
			for (float& v : scaled.samples.cornerValues)
				v *= toMm;
			// The display and the robust range are driven by the per-sample values when there are any (Local
			// thickness), otherwise by the per-triangle ones.
			const bool useSamples = !scaled.samples.values.empty();
			const std::vector<float>& rangeSource = useSamples ? scaled.samples.values : scaled.thicknessPerFace;
			for (size_t i = 0; i < rangeSource.size(); ++i)
			{
				const float v = rangeSource[i];
				if (!std::isfinite(v) || (!useSamples && !scaled.validPerFace[i]))
					continue;
				maxThickness = std::max(maxThickness, v);
				pooledThickness.push_back(v);
			}
			ThicknessWitnessSet& witness = _thicknessWitness[mesh];
			witness = ThicknessWitnessSet();
			if (!scaled.sampleWitness.empty())
			{
				witness.gridN = scaled.samples.gridN;
				witness.offset = scaled.samples.offset;
				witness.witness = scaled.sampleWitness;
				witness.toMm = toMm;
				witness.sphere = analysisParams.method == WallThicknessMethod::Sphere;
			}
			_lastLoggedThicknessMesh = nullptr;
		}
		else
		{
			rejectionNotes.append({ mesh->getName(), result->rejectionReason, NotesListBox::Severity::Warning });
		}
		perMesh.push_back({ mesh, scaled, outcome.snapshotKey });
	}

	if (_thicknessRejectionNote)
		_thicknessRejectionNote->setNotes(rejectionNotes);

	if (perMesh.empty())
	{
		if (anyStale)
		{
			QMessageBox::information(this, tr("Surface Analysis"),
				tr("Selection changed during computation - re-run Apply."));
		}
		return;
	}

	// 0 is the natural bottom of the range (zero thickness) rather than the
	// sampled data's own minimum - same reasoning as Deviation's range.
	// The TOP of the ramp is the 98th percentile rather than the maximum: one long ray (across the whole part)
	// used to set the scale and push every ordinary wall into the same blue-green. Values above it clamp to the
	// top colour, and the legend says so (">= max").
	float rangeMax = maxThickness > 1.0e-6f ? maxThickness : 1.0f;
	if (pooledThickness.size() >= 16)
	{
		const size_t rank = std::min(pooledThickness.size() - 1, static_cast<size_t>(std::ceil(pooledThickness.size() * 0.98)) - 1);
		std::nth_element(pooledThickness.begin(), pooledThickness.begin() + rank, pooledThickness.end());
		const float p98 = pooledThickness[rank];
		if (p98 > 1.0e-6f)
			rangeMax = p98;
	}
	_thicknessRangeMax = rangeMax;
	const bool highlight = _thicknessHighlightCheck && _thicknessHighlightCheck->isChecked();
	const float limitMm = _thicknessLimitSpin ? static_cast<float>(_thicknessLimitSpin->value()) : 1.0f;
	const int displayBands = highlight ? 0 : thicknessDisplayBands();

	// setAnalysisOverlayFlatColors() below uploads a real GPU buffer - same
	// makeCurrent()/doneCurrent() reasoning as every other Apply here. Runs
	// for EVERY surviving mesh regardless of anyValid below (including the
	// all-rejected case) - a REJECTED mesh must have whatever UNRELATED
	// overlay it happened to already be showing (e.g. a Draft Angle result
	// from an earlier Apply) cleared too, not left silently displayed under
	// this run's new (thickness-scaled) legend.
	viewport->makeCurrent();
	for (PerMesh& pm : perMesh)
	{
		if (!pm.result.succeeded)
		{
			_overlay.clearOverlay(pm.mesh);
			continue;
		}
		// Local thickness supplies sub-triangle samples (drawn as such); Normal ray only has one value per triangle.
		_overlay.applyRefinedResult(pm.mesh, pm.result.thicknessPerFace, pm.result.validPerFace, pm.result.samples,
			pm.key, 0.0f, highlight ? 2.0f * limitMm : rangeMax,
			highlight ? AnalysisColormap::Threshold : AnalysisColormap::Sequential, AnalysisKind::WallThickness,
			displayBands);
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

	updateThicknessLegendAndSummary();
}

double SurfaceAnalysisDialog::lengthScaleForMesh(SceneMesh* mesh) const
{
	if (!_modelViewer || !mesh)
		return 1.0;
	// Same resolution Mass Properties uses (a document default, a per-import unit, else the millimetre fallback).
	QJsonObject documentViewerState;
	if (_modelViewer->defaultImportUnit() != LengthUnit::Unknown)
		documentViewerState.insert(QStringLiteral("defaultImportUnit"), lengthUnitToString(_modelViewer->defaultImportUnit()));
	const ResolvedLengthUnit resolved = resolveEffectiveImportUnit(mesh, _modelViewer->sceneGraph(), documentViewerState);
	return lengthUnitToMillimeters(resolved.unit);
}

void SurfaceAnalysisDialog::onThicknessDisplayChanged()
{
	// Nothing to do until a Wall-Thickness result exists; otherwise switch every such overlay between the
	// continuous ramp and the pass/fail threshold map (values are already in mm and cached in _overlay).
	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	if (!viewport)
		return;

	const bool highlight = _thicknessHighlightCheck && _thicknessHighlightCheck->isChecked();
	const float limitMm = _thicknessLimitSpin ? static_cast<float>(_thicknessLimitSpin->value()) : 1.0f;
	const int displayBands = highlight ? 0 : thicknessDisplayBands();
	if (_thicknessDisplayCombo)
		_thicknessDisplayCombo->setEnabled(!highlight);
	if (_thicknessBandCountSpin)
		_thicknessBandCountSpin->setEnabled(!highlight && displayBands >= 2);

	bool any = false;
	viewport->makeCurrent();
	for (SceneMesh* mesh : _overlay.trackedMeshes())
	{
		AnalysisKind kind;
		if (!_overlay.kindOf(mesh, kind) || kind != AnalysisKind::WallThickness)
			continue;
		if (highlight)
			_overlay.recolor(mesh, 0.0f, 2.0f * limitMm, AnalysisColormap::Threshold);
		else
			_overlay.recolor(mesh, 0.0f, _thicknessRangeMax, AnalysisColormap::Sequential, displayBands);
		any = true;
	}
	viewport->doneCurrent();
	if (!any)
		return;

	updateThicknessLegendAndSummary();
	viewport->clearSurfaceAnalysisHoverReadout(); // its cached text/colour was computed from the old colouring
	viewport->update();
}

int SurfaceAnalysisDialog::thicknessDisplayBands() const
{
	if (!_thicknessDisplayCombo || !_thicknessDisplayCombo->currentData().toBool())
		return 0;
	return _thicknessBandCountSpin ? _thicknessBandCountSpin->value() : 10;
}

void SurfaceAnalysisDialog::updateThicknessLegendAndSummary()
{
	const bool highlight = _thicknessHighlightCheck && _thicknessHighlightCheck->isChecked();
	const float limitMm = _thicknessLimitSpin ? static_cast<float>(_thicknessLimitSpin->value()) : 1.0f;

	float thinnest = std::numeric_limits<float>::max();
	double areaBelow = 0.0, areaTotal = 0.0;
	int meshCount = 0;
	for (SceneMesh* mesh : _overlay.trackedMeshes())
	{
		AnalysisKind kind;
		std::vector<float> values;
		std::vector<bool> valid;
		if (!_overlay.kindOf(mesh, kind) || kind != AnalysisKind::WallThickness || !_overlay.scalarField(mesh, values, valid))
			continue;
		++meshCount;

		// Area weighting (not a face count): a CAD tessellation mixes huge and tiny triangles, and the share of
		// the SURFACE under the limit is what the number should mean.
		const std::vector<float> points = mesh->getTrsfPoints();
		const std::vector<unsigned int> indices = mesh->getIndices();
		const SubTriangleField* refined = _overlay.refinedFieldOf(mesh);
		for (size_t f = 0; f < values.size(); ++f)
		{
			if (f < valid.size() && !valid[f])
				continue;
			thinnest = std::min(thinnest, values[f]);
			const size_t base = f * 3;
			if (base + 2 >= indices.size())
				continue;
			const size_t ia = indices[base] * 3ull, ib = indices[base + 1] * 3ull, ic = indices[base + 2] * 3ull;
			if (ia + 2 >= points.size() || ib + 2 >= points.size() || ic + 2 >= points.size())
				continue;
			const QVector3D pa(points[ia], points[ia + 1], points[ia + 2]);
			const QVector3D pb(points[ib], points[ib + 1], points[ib + 2]);
			const QVector3D pc(points[ic], points[ic + 1], points[ic + 2]);
			const double area = 0.5 * QVector3D::crossProduct(pb - pa, pc - pa).length();
			if (refined && f < refined->gridN.size() && refined->gridN[f] > 0)
			{
				// Each sub-triangle of an n x n grid covers 1/n^2 of the triangle and has its own value.
				const size_t samples = static_cast<size_t>(refined->gridN[f]) * refined->gridN[f];
				const size_t first = refined->offset[f];
				if (first + samples <= refined->values.size())
				{
					const double share = area / static_cast<double>(samples);
					for (size_t k = 0; k < samples; ++k)
					{
						const float v = refined->values[first + k];
						if (std::isnan(v))
							continue;
						areaTotal += share;
						if (v < limitMm)
							areaBelow += share;
					}
					continue;
				}
			}
			areaTotal += area;
			if (values[f] < limitMm)
				areaBelow += area;
		}
	}
	if (meshCount == 0)
	{
		_legendLabel->setVisible(false);
		if (_thicknessSummaryLabel)
			_thicknessSummaryLabel->setVisible(false);
		return;
	}

	if (highlight)
	{
		_legendLabel->setPixmap(AnalysisColorRamp::thresholdLegend(280, 44,
			tr("< %1 mm").arg(limitMm, 0, 'g', 4), tr(">= %1 mm").arg(limitMm, 0, 'g', 4)));
	}
	else
	{
		_legendLabel->setPixmap(AnalysisColorRamp::legendGradient(280, 44, 0.0f, _thicknessRangeMax,
			AnalysisColormap::Sequential, tr(" mm"), true, thicknessDisplayBands()));
	}
	_legendLabel->setVisible(true);

	if (_thicknessSummaryLabel)
	{
		QString summary = tr("Thinnest wall found: %1 mm.").arg(thinnest, 0, 'g', 4);
		if (highlight && areaTotal > 0.0)
		{
			summary += QLatin1Char(' ') + tr("%1% of the analysed surface is thinner than %2 mm.")
				.arg(100.0 * areaBelow / areaTotal, 0, 'f', 1).arg(limitMm, 0, 'g', 4);
		}
		_thicknessSummaryLabel->setText(summary);
		_thicknessSummaryLabel->setVisible(true);
	}
}

void SurfaceAnalysisDialog::applyDraftAngleToSelection()
{
	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	if (!viewport)
		return;

	const std::vector<int> selected = selectedMeshIds();
	if (selected.empty())
	{
		QMessageBox::information(this, tr("Surface Analysis"), tr("Select one or more meshes first."));
		return;
	}

	const QVector3D pullDirection = currentPullDirection();
	const std::vector<SceneMesh*> meshStore = viewport->getMeshStore();

	QVariantMap params;
	params.insert(QStringLiteral("mode"), QStringLiteral("draftAngle"));
	params.insert(QStringLiteral("pullDirection"), QVariant::fromValue(pullDirection));

	// Captured synchronously, on THIS (main/GL) thread, right before
	// dispatch - see AnalysisMeshSnapshot's own doc comment for why a
	// background thread must never read a live SceneMesh directly.
	std::vector<AnalysisMeshSnapshot> snapshots;
	snapshots.reserve(selected.size());
	for (int id : selected)
		snapshots.push_back(captureAnalysisMeshSnapshot(meshStore.at(id), params));

	_deletedWhileComputing.clear();
	const QString originalText = _applyDraftButton->text();
	setComputationInFlight(true, _applyDraftButton, tr("Cancel"));

	AnalysisComputeSession session(this);
	_activeSession = &session;

	const std::vector<AnalysisComputeSession::PerMeshOutcome> outcomes = session.runBlocking(
		std::move(snapshots),
		[pullDirection](const AnalysisMeshSnapshot& snapshot, const std::atomic<bool>&) -> std::any
		{
			return DraftAngleAnalyzer::computeDraftAnglesDegrees(snapshot.points, snapshot.indices, pullDirection);
		});

	_activeSession = nullptr;
	setComputationInFlight(false, _applyDraftButton, originalText);

	// A cancelled run returns an empty outcome list (AnalysisComputeSession
	// never half-applies a batch - see its own doc comment) - nothing to do
	// either way.
	if (outcomes.empty())
		return;

	struct PerMesh { SceneMesh* mesh; std::vector<float> angles; SurfaceAnalysisOverlay::CacheKey key; };
	std::vector<PerMesh> perMesh;
	perMesh.reserve(outcomes.size());

	float minAngle = std::numeric_limits<float>::max();
	float maxAngle = std::numeric_limits<float>::lowest();
	bool anyFace = false;

	for (const AnalysisComputeSession::PerMeshOutcome& outcome : outcomes)
	{
		SceneMesh* mesh = outcome.meshHandle;
		if (!mesh || _deletedWhileComputing.contains(mesh))
			continue; // deleted while this ran - see _deletedWhileComputing's own doc comment

		// Re-validate against the mesh's CURRENT state before trusting a
		// result computed on a background thread - it may have been
		// transformed or otherwise edited while this ran. See
		// SurfaceAnalysisOverlay::computeCurrentKey()'s own doc comment.
		const SurfaceAnalysisOverlay::CacheKey currentKey = SurfaceAnalysisOverlay::computeCurrentKey(mesh, params);
		if (!(currentKey == outcome.snapshotKey))
			continue; // stale - mesh changed mid-computation, discard this result rather than apply it

		const std::vector<float>* angles = std::any_cast<std::vector<float>>(&outcome.result);
		if (!angles)
			continue;

		for (float a : *angles)
		{
			minAngle = std::min(minAngle, a);
			maxAngle = std::max(maxAngle, a);
			anyFace = true;
		}
		perMesh.push_back({ mesh, *angles, outcome.snapshotKey });
	}

	if (!anyFace)
	{
		// perMesh.empty() specifically means every outcome was discarded as
		// stale/deleted above, not just "no triangles in this selection" -
		// worth telling the user, unlike the latter (which silently returns,
		// same as before this dialog had a background worker at all).
		if (perMesh.empty())
		{
			QMessageBox::information(this, tr("Surface Analysis"),
				tr("Selection changed during computation - re-run Apply."));
		}
		return;
	}

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
		_overlay.applyFlatResult(pm.mesh, pm.angles, {}, pm.key, rangeMin, rangeMax, AnalysisColormap::Diverging, AnalysisKind::DraftAngle);
	viewport->doneCurrent();

	_legendLabel->setPixmap(AnalysisColorRamp::legendGradient(280, 44, rangeMin, rangeMax, AnalysisColormap::Diverging, QStringLiteral("°")));
	_legendLabel->setVisible(true);

	viewport->update();
}

void SurfaceAnalysisDialog::onReferenceMeshRejected()
{
	// Not silent: a mesh cannot be compared with itself, and the picker did not take it.
	QMessageBox::warning(this, tr("Surface Analysis"),
		tr("The reference mesh must be a different mesh from the one being compared - a mesh cannot be compared with itself."));
}

void SurfaceAnalysisDialog::syncReferenceExclusions()
{
	if (_referenceBox && _selectionBox)
		_referenceBox->setExcludedUuids(_selectionBox->meshUuids());
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
	const std::vector<int> selected = selectedMeshIds();
	if (selected.size() != 1)
	{
		QMessageBox::information(this, tr("Surface Analysis"),
			tr("Select exactly one mesh to compare (the scan/comparison side)."));
		return;
	}
	const QVector<QUuid> referenceList = _referenceBox ? _referenceBox->meshUuids() : QVector<QUuid>();
	if (referenceList.isEmpty())
	{
		QMessageBox::information(this, tr("Surface Analysis"),
			tr("Pick a reference mesh with the Reference mesh box first."));
		return;
	}

	const std::vector<SceneMesh*> meshStore = viewport->getMeshStore();
	SceneMesh* sampledMesh = meshStore.at(selected.front());
	// Resolved by UUID, never a mesh-store index (an import/delete reindexes the store). getMeshByUuid() returns
	// nullptr if the chosen mesh was deleted since it was picked, which the message below covers.
	const QUuid referenceUuid = referenceList.first();
	SceneMesh* referenceMesh = referenceUuid.isNull() ? nullptr : viewport->getMeshByUuid(referenceUuid);
	if (!referenceMesh)
	{
		QMessageBox::information(this, tr("Surface Analysis"),
			tr("The chosen reference mesh is no longer available - pick another one."));
		return;
	}
	if (referenceMesh == sampledMesh)
	{
		onReferenceMeshRejected();
		return;
	}

	QVariantMap params;
	params.insert(QStringLiteral("mode"), QStringLiteral("deviation"));

	// Only sampledMesh gets its own AnalysisMeshSnapshot/result - Deviation
	// computes ONE result (for the sampled mesh), not one per input mesh, so
	// referenceMesh's geometry is captured here (main thread, before
	// dispatch - same "never a live pointer read from the worker thread"
	// principle AnalysisMeshSnapshot itself follows) and closed over by
	// value in the TaskFn below, rather than becoming a second snapshot in
	// the list.
	std::vector<AnalysisMeshSnapshot> snapshots;
	snapshots.push_back(captureAnalysisMeshSnapshot(sampledMesh, params, referenceMesh));
	const std::vector<float> referencePoints = referenceMesh->getTrsfPoints();
	const std::vector<unsigned int> referenceIndices = referenceMesh->getIndices();

	_deletedWhileComputing.clear();
	const QString originalText = _applyDeviationButton->text();
	setComputationInFlight(true, _applyDeviationButton, tr("Cancel"));

	AnalysisComputeSession session(this);
	_activeSession = &session;

	const std::vector<AnalysisComputeSession::PerMeshOutcome> outcomes = session.runBlocking(
		std::move(snapshots),
		[referencePoints, referenceIndices](const AnalysisMeshSnapshot& snapshot, const std::atomic<bool>&) -> std::any
		{
			return DeviationAnalyzer::computeDeviation(snapshot.points, snapshot.indices, referencePoints, referenceIndices);
		});

	_activeSession = nullptr;
	setComputationInFlight(false, _applyDeviationButton, originalText);

	if (outcomes.empty())
		return; // cancelled - AnalysisComputeSession never half-applies a batch

	const AnalysisComputeSession::PerMeshOutcome& outcome = outcomes.front();
	SceneMesh* mesh = outcome.meshHandle;
	if (!mesh || _deletedWhileComputing.contains(mesh) || _deletedWhileComputing.contains(referenceMesh))
	{
		QMessageBox::information(this, tr("Surface Analysis"),
			tr("Selection changed during computation - re-run Apply."));
		return;
	}

	// Re-validate against both meshes' CURRENT state - either the sampled or
	// the reference mesh may have been transformed/edited while this ran.
	// See SurfaceAnalysisOverlay::computeCurrentKey()'s own doc comment.
	const SurfaceAnalysisOverlay::CacheKey currentKey =
		SurfaceAnalysisOverlay::computeCurrentKey(mesh, params, referenceMesh);
	if (!(currentKey == outcome.snapshotKey))
	{
		QMessageBox::information(this, tr("Surface Analysis"),
			tr("Selection changed during computation - re-run Apply."));
		return;
	}

	const std::vector<float>* distances = std::any_cast<std::vector<float>>(&outcome.result);
	if (!distances || distances->empty())
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
	for (float d : *distances)
		maxDist = std::max(maxDist, d);
	const float rangeMax = maxDist > 1.0e-6f ? maxDist : 1.0f;

	// setAnalysisOverlayColors() below uploads a real GPU buffer - same
	// makeCurrent()/doneCurrent() reasoning as applyDraftAngleToSelection().
	viewport->makeCurrent();
	_overlay.applyResult(mesh, *distances, {}, outcome.snapshotKey, 0.0f, rangeMax, AnalysisColormap::Sequential, AnalysisKind::Deviation);
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

	// Every mesh showing a result from this dialog, not only the currently selected ones: clicking in the viewport
	// to inspect a result changes the selection, and a Clear that then skipped the deselected meshes left their
	// overlay on screen. (clearAllOverlays() also drops the zebra-stripe state, the wall-thickness hover log
	// bookkeeping and the cached hover readout.)
	clearAllOverlays();

	_zebraStripeToggle->blockSignals(true);
	_zebraStripeToggle->setChecked(false);
	_zebraStripeToggle->blockSignals(false);
	_legendLabel->setVisible(false);
	if (_curvatureRepairNote)
		_curvatureRepairNote->clearNotes();
	if (_thicknessRejectionNote)
		_thicknessRejectionNote->clearNotes();
	if (_thicknessSummaryLabel)
		_thicknessSummaryLabel->setVisible(false);
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
	_thicknessWitness.clear();
	_lastLoggedThicknessMesh = nullptr;

	if (ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr)
	{
		viewport->clearSurfaceAnalysisHoverReadout();
		viewport->update();
	}
}

void SurfaceAnalysisDialog::selectMode(const QString& mode)
{
    Mode selected;
    if (mode == QLatin1String("curvature")) selected = Mode::Curvature;
    else if (mode == QLatin1String("thickness")) selected = Mode::WallThickness;
    else if (mode == QLatin1String("deviation")) selected = Mode::Deviation;
    else return;
    // Reopening the current page must retain its displayed result and legend.
    if (selected == currentMode()) return;
    if (auto* button = _modeGroup->button(static_cast<int>(selected))) {
        button->setChecked(true);
        onModeChanged();
    }
}
