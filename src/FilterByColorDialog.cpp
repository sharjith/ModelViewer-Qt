#include "FilterByColorDialog.h"
#include "MeshColorUtils.h"
#include "ModelViewer.h"
#include "ViewportWidget.h"
#include "SceneMesh.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QColorDialog>
#include <QColor>
#include <QFont>
#include <QShowEvent>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
	constexpr int kToleranceSliderMax = 100;
	// Slider is 0-100, mapped to an epsilon of 0.0-0.5 in linear RGB space -
	// full RGB distance between opposite corners is sqrt(3) =~ 1.73, so 0.5
	// already covers a generous practical range without a slider that's
	// mostly dead space at the high end.
	constexpr float kToleranceSliderToEpsilon = 0.5f / kToleranceSliderMax;

	QColor toQColor(const QVector3D& c)
	{
		return QColor::fromRgbF(
			std::clamp(c.x(), 0.0f, 1.0f),
			std::clamp(c.y(), 0.0f, 1.0f),
			std::clamp(c.z(), 0.0f, 1.0f));
	}

	// Walks up the parent chain from a widget inside the MDI area to find the QMdiArea itself -
	// same helper as ShrinkWrapDialog.cpp/FilterByMaterialDialog.cpp, redeclared locally per that
	// file's own convention.
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

FilterByColorDialog::FilterByColorDialog(ModelViewer* modelViewer,
                                          const QVector3D& initialColor,
                                          QWidget* parent)
	: QDialog(parent)
	, _modelViewer(modelViewer)
	, _targetColor(initialColor)
{
	setWindowTitle(tr("Filter by Color"));
	resize(400, 320);
	setAttribute(Qt::WA_DeleteOnClose);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(16, 16, 16, 16);
	layout->setSpacing(12);

	auto* introLabel = new QLabel(tr("Preview every mesh in the scene whose color matches the "
	                                  "target, within a tolerance, then Show Only or Hide the "
	                                  "result:"), this);
	introLabel->setWordWrap(true);
	layout->addWidget(introLabel);

	auto* colorGroup = new QGroupBox(tr("Target Color"), this);
	auto* colorLayout = new QVBoxLayout(colorGroup);
	_swatch = new QPushButton(colorGroup);
	_swatch->setFixedHeight(56);
	_swatch->setCursor(Qt::PointingHandCursor);
	_swatch->setToolTip(tr("Click to pick a color"));
	colorLayout->addWidget(_swatch);
	layout->addWidget(colorGroup);

	auto* toleranceGroup = new QGroupBox(tr("Match Tolerance"), this);
	auto* toleranceRow = new QHBoxLayout(toleranceGroup);
	_toleranceSlider = new QSlider(Qt::Horizontal, toleranceGroup);
	_toleranceSlider->setRange(0, kToleranceSliderMax);
	_toleranceSlider->setValue(static_cast<int>(_tolerance / kToleranceSliderToEpsilon));
	toleranceRow->addWidget(_toleranceSlider, 1);
	_toleranceValueLabel = new QLabel(QString::number(_tolerance, 'f', 2), toleranceGroup);
	_toleranceValueLabel->setMinimumWidth(40);
	_toleranceValueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	toleranceRow->addWidget(_toleranceValueLabel);
	layout->addWidget(toleranceGroup);

	_matchCountLabel = new QLabel(this);
	_matchCountLabel->setAlignment(Qt::AlignCenter);
	_matchCountLabel->setWordWrap(true);
	layout->addWidget(_matchCountLabel);

	layout->addStretch();

	auto* buttonRow = new QHBoxLayout();
	buttonRow->addStretch();
	_showOnlyButton = new QPushButton(tr("Show Only"), this);
	_hideButton = new QPushButton(tr("Hide"), this);
	buttonRow->addWidget(_showOnlyButton);
	buttonRow->addWidget(_hideButton);
	layout->addLayout(buttonRow);

	connect(_swatch, &QPushButton::clicked, this, &FilterByColorDialog::onPickColorClicked);
	connect(_toleranceSlider, &QSlider::valueChanged, this, &FilterByColorDialog::onToleranceChanged);
	connect(_showOnlyButton, &QPushButton::clicked, this, &FilterByColorDialog::onShowOnlyClicked);
	connect(_hideButton, &QPushButton::clicked, this, &FilterByColorDialog::onHideClicked);

	// Hide/show this dialog as its OWN document's MDI subwindow loses/gains focus - mirrors
	// ShrinkWrapDialog's identical mechanism.
	if (_modelViewer)
	{
		if (QMdiArea* mdiArea = findMdiArea(_modelViewer))
			connect(mdiArea, &QMdiArea::subWindowActivated, this, &FilterByColorDialog::onActiveSubWindowChanged);
	}

	updateSwatch();
	updateMatches();
}

void FilterByColorDialog::showEvent(QShowEvent* event)
{
	QDialog::showEvent(event);
	// The scene may have changed while this dialog was hidden behind
	// another document tab - recompute against a fresh mesh store rather
	// than trusting a stale snapshot.
	updateMatches();
}

void FilterByColorDialog::onActiveSubWindowChanged(QMdiSubWindow* activeSubWindow)
{
	const bool isOwnDocumentActive = _modelViewer
		&& activeSubWindow
		&& activeSubWindow->widget() == static_cast<QWidget*>(_modelViewer);
	setVisible(isOwnDocumentActive);
}

void FilterByColorDialog::onPickColorClicked()
{
	const QColor picked = QColorDialog::getColor(toQColor(_targetColor), this, tr("Pick Target Color"));
	if (!picked.isValid())
		return;

	_targetColor = QVector3D(static_cast<float>(picked.redF()),
	                          static_cast<float>(picked.greenF()),
	                          static_cast<float>(picked.blueF()));
	updateSwatch();
	updateMatches();
}

void FilterByColorDialog::onToleranceChanged(int sliderValue)
{
	_tolerance = sliderValue * kToleranceSliderToEpsilon;
	_toleranceValueLabel->setText(QString::number(_tolerance, 'f', 2));
	updateMatches();
}

void FilterByColorDialog::updateSwatch()
{
	// Background AND border both driven through the stylesheet (not
	// setAutoFillBackground()+palette for the background) - mixing a
	// palette-painted background with a stylesheet border on the same
	// widget risks the stylesheet engine taking over painting entirely and
	// silently dropping the palette-set color, which would make the swatch
	// stop showing the picked color at all. One mechanism, no ambiguity.
	// No text on the button itself (a solid color bar avoids ever having to
	// pick a contrasting text color against an arbitrary background) - the
	// tooltip, pointing-hand cursor, and :hover border are the affordance.
	const QString colorName = toQColor(_targetColor).name();
	_swatch->setStyleSheet(QStringLiteral(
		"QPushButton { background-color: %1; border: 1px solid palette(mid); border-radius: 4px; }"
		"QPushButton:hover { border: 1px solid palette(highlight); }")
		.arg(colorName));
}

void FilterByColorDialog::updateMatches()
{
	if (!_modelViewer || !_modelViewer->getViewportWidget())
		return;

	std::vector<SceneMesh*> meshStore = _modelViewer->getViewportWidget()->getMeshStore();

	std::vector<int> matchingIndices;
	for (int i = 0; i < static_cast<int>(meshStore.size()); ++i)
	{
		const QVector3D color = meshRepresentativeColor(meshStore[i]);
		if ((color - _targetColor).length() <= _tolerance)
			matchingIndices.push_back(i);
	}

	const bool hasMatches = !matchingIndices.empty();
	_matchCountLabel->setText(hasMatches
		? tr("%1 mesh(es) match.").arg(matchingIndices.size())
		: tr("No meshes match this color within the current tolerance."));

	// Theme-safe emphasis via palette color group rather than a hardcoded
	// hex - the "disabled" text color reads as a muted/secondary tone in
	// both light and dark themes, same as any grayed-out label.
	QPalette pal = _matchCountLabel->palette();
	pal.setColor(QPalette::WindowText, pal.color(hasMatches ? QPalette::Active : QPalette::Disabled, QPalette::WindowText));
	_matchCountLabel->setPalette(pal);
	QFont font = _matchCountLabel->font();
	font.setBold(hasMatches);
	_matchCountLabel->setFont(font);

	_showOnlyButton->setEnabled(hasMatches);
	_hideButton->setEnabled(hasMatches);

	const QSet<int> newSelection(matchingIndices.cbegin(), matchingIndices.cend());
	const std::vector<int> currentIds = _modelViewer->getSelectedIDs();
	const QSet<int> currentSelection(currentIds.cbegin(), currentIds.cend());
	if (newSelection == currentSelection)
		return;

	// mergeSource == this: consecutive color/tolerance tweaks while this
	// dialog stays open collapse into one undo step (see SelectionCommand's
	// mergeWith()).
	_modelViewer->setSelectionWithUndo(newSelection, this);
}

void FilterByColorDialog::onShowOnlyClicked()
{
	if (_modelViewer)
		_modelViewer->showOnlySelectedItems();
}

void FilterByColorDialog::onHideClicked()
{
	if (_modelViewer)
		_modelViewer->hideSelectedItems();
}
