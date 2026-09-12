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
#include <QToolButton>
#include <QCheckBox>
#include <QSlider>
#include <QListWidget>
#include <QAbstractItemView>
#include <QColorDialog>
#include <QColor>
#include <QIcon>
#include <QFont>
#include <QMenu>
#include <QClipboard>
#include <QGuiApplication>
#include <QShowEvent>
#include <QCloseEvent>
#include <QEvent>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QSet>
#include <QSignalBlocker>
#include <QSettings>

#include <algorithm>
#include <cmath>
#include <numeric>
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
                                          const QVector<QVector3D>& initialColors,
                                          QWidget* parent)
	: QDialog(parent)
	, _modelViewer(modelViewer)
	, _colors(initialColors)
{
	setWindowTitle(tr("Filter by Color"));
	resize(400, 420);
	setAttribute(Qt::WA_DeleteOnClose);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(16, 16, 16, 16);
	layout->setSpacing(12);

	auto* introLabel = new QLabel(tr("Add one or more target colors to preview every mesh in the "
	                                  "scene that matches any of them, within a tolerance, then "
	                                  "Show Only or Hide the result:"), this);
	introLabel->setWordWrap(true);
	layout->addWidget(introLabel);

	_colorsGroup = new QGroupBox(tr("Target Colors"), this);
	auto* colorsLayout = new QVBoxLayout(_colorsGroup);

	auto* sortRow = new QHBoxLayout();
	sortRow->addStretch(1);
	_sortByMatchCountCheck = new QCheckBox(tr("Sort by matches"), _colorsGroup);
	_sortByMatchCountCheck->setToolTip(tr("Sort by mesh match count (most matches first) instead of the order added"));
	sortRow->addWidget(_sortByMatchCountCheck);
	colorsLayout->addLayout(sortRow);

	_list = new QListWidget(_colorsGroup);
	_list->setSelectionMode(QAbstractItemView::NoSelection);
	_list->setFocusPolicy(Qt::NoFocus);
	// Matches FilterByMaterialDialog's context-menu setup exactly (policy +
	// signal on the list widget itself, not its viewport - see that class's
	// fix for why the viewport combination silently never fired).
	_list->setContextMenuPolicy(Qt::CustomContextMenu);
	colorsLayout->addWidget(_list);

	auto* addRow = new QHBoxLayout();
	_addButton = new QPushButton(tr("+ Add Color..."), _colorsGroup);
	addRow->addWidget(_addButton, 1);

	_pickFromMeshButton = new QToolButton(_colorsGroup);
	_pickFromMeshButton->setIcon(QIcon(QStringLiteral(":/icons/res/eye_dropper.png")));
	_pickFromMeshButton->setCheckable(true);
	_pickFromMeshButton->setToolTip(tr("Pick colors from meshes in the viewport.\n"
	                                    "Click one or more meshes to add their exact color.\n"
	                                    "Click this button again (or press Esc) when done."));
	// Square, matching _addButton's height, rather than the icon-only
	// default size - so the two sit visually level in the row instead of a
	// small icon button looking undersized beside a full-height text button.
	const int addButtonHeight = _addButton->sizeHint().height();
	_pickFromMeshButton->setFixedSize(addButtonHeight, addButtonHeight);
	_pickFromMeshButton->setIconSize(QSize(addButtonHeight - 12, addButtonHeight - 12));
	addRow->addWidget(_pickFromMeshButton);
	colorsLayout->addLayout(addRow);

	_autoDetectButton = new QPushButton(tr("Auto-Detect Colors in Scene"), _colorsGroup);
	_autoDetectButton->setToolTip(tr("Add every distinct color found in the scene.\n"
	                                  "Then remove the ones you don't want with each row's × button."));
	colorsLayout->addWidget(_autoDetectButton);

	layout->addWidget(_colorsGroup, 1);

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

	auto* buttonRow = new QHBoxLayout();
	buttonRow->addStretch();
	_showOnlyButton = new QPushButton(tr("Show Only"), this);
	_hideButton = new QPushButton(tr("Hide"), this);
	buttonRow->addWidget(_showOnlyButton);
	buttonRow->addWidget(_hideButton);
	layout->addLayout(buttonRow);

	connect(_addButton, &QPushButton::clicked, this, &FilterByColorDialog::onAddColorClicked);
	connect(_pickFromMeshButton, &QToolButton::toggled, this, &FilterByColorDialog::onPickFromMeshToggled);
	connect(_autoDetectButton, &QPushButton::clicked, this, &FilterByColorDialog::onAutoDetectClicked);
	connect(_toleranceSlider, &QSlider::valueChanged, this, &FilterByColorDialog::onToleranceChanged);
	connect(_sortByMatchCountCheck, &QCheckBox::toggled, this, &FilterByColorDialog::rebuildColorList);
	connect(_list, &QWidget::customContextMenuRequested, this, &FilterByColorDialog::onListContextMenuRequested);
	connect(_showOnlyButton, &QPushButton::clicked, this, &FilterByColorDialog::onShowOnlyClicked);
	connect(_hideButton, &QPushButton::clicked, this, &FilterByColorDialog::onHideClicked);

	// Hide/show this dialog as its OWN document's MDI subwindow loses/gains focus - mirrors
	// ShrinkWrapDialog's identical mechanism.
	if (_modelViewer)
	{
		if (QMdiArea* mdiArea = findMdiArea(_modelViewer))
			connect(mdiArea, &QMdiArea::subWindowActivated, this, &FilterByColorDialog::onActiveSubWindowChanged);

		// Owns its ModelViewer/ViewportWidget outright (unlike the shared-
		// panel material eyedropper), so these connect directly here rather
		// than through MainWindow's rebind-dispatch mechanism.
		if (ViewportWidget* viewport = _modelViewer->getViewportWidget())
		{
			connect(viewport, &ViewportWidget::colorPicked, this, &FilterByColorDialog::onColorPicked);
			connect(viewport, &ViewportWidget::colorPickArmedChanged, this, &FilterByColorDialog::onColorPickArmedChanged);
		}
	}

	loadSettings();
	rebuildColorList();
}

void FilterByColorDialog::showEvent(QShowEvent* event)
{
	QDialog::showEvent(event);
	// The scene may have changed while this dialog was hidden behind
	// another document tab - recompute against a fresh mesh store rather
	// than trusting a stale snapshot. _colors itself is user-authored state
	// and doesn't need rebuilding, just re-matching.
	updateMatches();
}

void FilterByColorDialog::onActiveSubWindowChanged(QMdiSubWindow* activeSubWindow)
{
	const bool isOwnDocumentActive = _modelViewer
		&& activeSubWindow
		&& activeSubWindow->widget() == static_cast<QWidget*>(_modelViewer);
	setVisible(isOwnDocumentActive);
}

void FilterByColorDialog::changeEvent(QEvent* event)
{
	QDialog::changeEvent(event);
	if (event->type() == QEvent::ActivationChange && isActiveWindow())
		rebuildColorList();
}

void FilterByColorDialog::closeEvent(QCloseEvent* event)
{
	if (_modelViewer && _modelViewer->getViewportWidget())
		_modelViewer->getViewportWidget()->setColorPickArmed(false);
	saveSettings();
	QDialog::closeEvent(event);
}

void FilterByColorDialog::reject()
{
	if (_modelViewer && _modelViewer->getViewportWidget())
		_modelViewer->getViewportWidget()->setColorPickArmed(false);
	saveSettings();
	QDialog::reject();
}

void FilterByColorDialog::loadSettings()
{
	QSettings settings;
	const QByteArray geometry = settings.value("filterByColor/geometry", QByteArray()).toByteArray();
	if (!geometry.isEmpty())
		restoreGeometry(geometry);

	// Blocked: this runs before rebuildColorList()'s first call in the
	// constructor, so toggled() firing here would just re-enter a
	// rebuildColorList() that's about to run anyway right after loadSettings().
	const QSignalBlocker blocker(_sortByMatchCountCheck);
	_sortByMatchCountCheck->setChecked(settings.value("filterByColor/sortByMatchCount", false).toBool());
}

void FilterByColorDialog::saveSettings()
{
	QSettings settings;
	settings.setValue("filterByColor/geometry", saveGeometry());
	settings.setValue("filterByColor/sortByMatchCount", _sortByMatchCountCheck->isChecked());
}

void FilterByColorDialog::onAddColorClicked()
{
	const QColor picked = QColorDialog::getColor(Qt::white, this, tr("Add Target Color"));
	if (!picked.isValid())
		return;

	_colors.push_back(QVector3D(static_cast<float>(picked.redF()),
	                             static_cast<float>(picked.greenF()),
	                             static_cast<float>(picked.blueF())));
	rebuildColorList();
}

void FilterByColorDialog::onAutoDetectClicked()
{
	if (!_modelViewer || !_modelViewer->getViewportWidget())
		return;

	const std::vector<SceneMesh*> meshStore = _modelViewer->getViewportWidget()->getMeshStore();
	QVector<QVector3D> sceneColors;
	sceneColors.reserve(static_cast<int>(meshStore.size()));
	for (SceneMesh* mesh : meshStore)
		sceneColors.push_back(meshRepresentativeColor(mesh));

	_colors = dedupedColors(_colors, sceneColors);
	rebuildColorList();
}

void FilterByColorDialog::onPickFromMeshToggled(bool checked)
{
	if (_modelViewer && _modelViewer->getViewportWidget())
		_modelViewer->getViewportWidget()->setColorPickArmed(checked);
}

void FilterByColorDialog::onColorPicked(const QVector3D& color)
{
	// Only while THIS dialog is the one that armed picking - ViewportWidget
	// is per-document and this dialog owns it outright for its whole
	// lifetime, so in practice this signal only ever reaches an instance
	// that's either armed or already closed (harmless if the latter,
	// Qt disconnects on destruction).
	if (!_pickFromMeshButton->isChecked())
		return;

	_colors.push_back(color);
	rebuildColorList();
}

void FilterByColorDialog::onColorPickArmedChanged(bool armed)
{
	// Blocked so this doesn't re-trigger onPickFromMeshToggled() ->
	// setColorPickArmed() right back at ViewportWidget - this slot exists
	// purely to reflect state that changed FOR OTHER reasons (another armed
	// tool's mutual-exclusion clearing), not to re-request it.
	const QSignalBlocker blocker(_pickFromMeshButton);
	_pickFromMeshButton->setChecked(armed);
}

void FilterByColorDialog::removeColorAt(int index)
{
	if (index < 0 || index >= _colors.size())
		return;
	_colors.removeAt(index);
	rebuildColorList();
}

void FilterByColorDialog::rebuildColorList()
{
	// Per-color match counts, computed independently of each other (NOT the
	// union - a mesh matching two listed colors counts toward both) so a
	// color that isn't actually catching anything (a slightly-off guess at
	// a shaded part's true representative color, or a genuine app bug) is
	// visible per-row instead of silently vanishing into the aggregate
	// count below the list.
	std::vector<int> perColorMatchCount(_colors.size(), 0);
	if (_modelViewer && _modelViewer->getViewportWidget())
	{
		const std::vector<SceneMesh*> meshStore = _modelViewer->getViewportWidget()->getMeshStore();
		for (SceneMesh* mesh : meshStore)
		{
			const QVector3D color = meshRepresentativeColor(mesh);
			for (int c = 0; c < _colors.size(); ++c)
			{
				if ((color - _colors[c]).length() <= _tolerance)
					++perColorMatchCount[c];
			}
		}
	}

	// Reorders _colors itself (not just a display copy) - harmless to the
	// union-match semantics, which don't care about list order, and keeps
	// the remove buttons' captured-index closures below correct without any
	// extra indirection.
	if (_sortByMatchCountCheck->isChecked())
	{
		std::vector<int> order(_colors.size());
		std::iota(order.begin(), order.end(), 0);
		std::stable_sort(order.begin(), order.end(), [&perColorMatchCount](int a, int b) {
			return perColorMatchCount[a] > perColorMatchCount[b];
		});

		QVector<QVector3D> sortedColors;
		std::vector<int> sortedCounts;
		sortedColors.reserve(_colors.size());
		sortedCounts.reserve(_colors.size());
		for (int idx : order)
		{
			sortedColors.push_back(_colors[idx]);
			sortedCounts.push_back(perColorMatchCount[idx]);
		}
		_colors = sortedColors;
		perColorMatchCount = sortedCounts;
	}

	_list->clear();
	for (int i = 0; i < _colors.size(); ++i)
	{
		const QVector3D color = _colors[i];
		// QListWidgetItem(QListWidget*) already inserts itself into _list -
		// an earlier version also called _list->addItem(item) here, which
		// double-inserted the same item into the list's model and corrupted
		// it on the next clear() (confirmed real bug: adding/removing a
		// color silently stopped affecting anything after the first one).
		auto* item = new QListWidgetItem(_list);
		item->setData(Qt::UserRole, i); // for onListContextMenuRequested()'s itemAt() -> _colors[index] mapping

		auto* rowWidget = new QWidget(_list);
		auto* rowLayout = new QHBoxLayout(rowWidget);
		rowLayout->setContentsMargins(6, 3, 6, 3);
		rowLayout->setSpacing(8);

		auto* swatch = new QLabel(rowWidget);
		swatch->setFixedSize(24, 24);
		swatch->setStyleSheet(QStringLiteral(
			"background-color: %1; border: 1px solid palette(mid); border-radius: 4px;")
			.arg(toQColor(color).name()));
		rowLayout->addWidget(swatch);

		auto* hexLabel = new QLabel(tr("%1 (%2 mesh(es))")
			.arg(toQColor(color).name().toUpper())
			.arg(perColorMatchCount[i]), rowWidget);
		rowLayout->addWidget(hexLabel, 1);

		auto* removeButton = new QToolButton(rowWidget);
		removeButton->setText(QStringLiteral("×")); // "x" (multiplication sign)
		removeButton->setToolTip(tr("Remove this color"));
		removeButton->setAutoRaise(true);
		// Captures the row's index directly rather than looking it up via
		// the item pointer at click time - safe here because rebuildColorList()
		// is the only thing that ever changes _colors/_list, and every
		// click goes through it immediately after, so no other row can
		// shift out from under a stale captured index between rebuilds.
		connect(removeButton, &QToolButton::clicked, this, [this, i]() { removeColorAt(i); });
		rowLayout->addWidget(removeButton);

		item->setSizeHint(rowWidget->sizeHint());
		_list->setItemWidget(item, rowWidget);
	}

	updateGroupBoxTitle();
	updateMatches();
}

void FilterByColorDialog::updateGroupBoxTitle()
{
	if (!_colorsGroup)
		return;
	_colorsGroup->setTitle(_colors.isEmpty()
		? tr("Target Colors")
		: tr("Target Colors (%1)").arg(_colors.size()));
}

void FilterByColorDialog::onToleranceChanged(int sliderValue)
{
	_tolerance = sliderValue * kToleranceSliderToEpsilon;
	_toleranceValueLabel->setText(QString::number(_tolerance, 'f', 2));
	// Full rebuild, not just updateMatches() - the per-row match counts
	// depend on tolerance too.
	rebuildColorList();
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
		for (const QVector3D& target : _colors)
		{
			if ((color - target).length() <= _tolerance)
			{
				matchingIndices.push_back(i);
				break;
			}
		}
	}

	const bool hasColors = !_colors.isEmpty();
	const bool hasMatches = !matchingIndices.empty();
	_matchCountLabel->setText(!hasColors
		? tr("Add a color to start filtering.")
		: hasMatches
			? tr("%1 mesh(es) match.").arg(matchingIndices.size())
			: tr("No meshes match any listed color within the current tolerance."));

	// Theme-safe emphasis via palette color group rather than a hardcoded
	// hex - the "disabled" text color reads as a muted/secondary tone in
	// both light and dark themes, same as any grayed-out label.
	const bool emphasize = hasColors && hasMatches;
	QPalette pal = _matchCountLabel->palette();
	pal.setColor(QPalette::WindowText, pal.color(emphasize ? QPalette::Active : QPalette::Disabled, QPalette::WindowText));
	_matchCountLabel->setPalette(pal);
	QFont font = _matchCountLabel->font();
	font.setBold(emphasize);
	_matchCountLabel->setFont(font);

	_showOnlyButton->setEnabled(emphasize);
	_hideButton->setEnabled(emphasize);

	// An empty color list is "not armed" (see this class's header doc
	// comment) - leave the live viewport selection exactly as it was rather
	// than forcing it to empty, so opening this dialog (or removing every
	// listed color) never clobbers whatever the user already had selected.
	if (!hasColors)
		return;

	const QSet<int> newSelection(matchingIndices.cbegin(), matchingIndices.cend());
	const std::vector<int> currentIds = _modelViewer->getSelectedIDs();
	const QSet<int> currentSelection(currentIds.cbegin(), currentIds.cend());
	if (newSelection == currentSelection)
		return;

	// mergeSource == this: consecutive add/remove/tolerance tweaks while
	// this dialog stays open collapse into one undo step (see
	// SelectionCommand's mergeWith()).
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

void FilterByColorDialog::onListContextMenuRequested(const QPoint& pos)
{
	QListWidgetItem* item = _list->itemAt(pos);
	if (!item)
		return;

	const int index = item->data(Qt::UserRole).toInt();
	if (index < 0 || index >= _colors.size())
		return;

	QMenu menu(this);
	QAction* copyAction = menu.addAction(tr("Copy Hex Color"));
	QAction* removeAction = menu.addAction(tr("Remove Color"));
	QAction* chosen = menu.exec(_list->viewport()->mapToGlobal(pos));
	if (chosen == copyAction)
		QGuiApplication::clipboard()->setText(toQColor(_colors[index]).name().toUpper());
	else if (chosen == removeAction)
		removeColorAt(index); // index is still valid here - nothing else touches _colors while the menu is open
}
