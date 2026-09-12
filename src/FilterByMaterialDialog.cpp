#include "FilterByMaterialDialog.h"
#include "MaterialGrouping.h"
#include "ModelViewer.h"
#include "ViewportWidget.h"
#include "SceneMesh.h"
#include "Material.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QListWidget>
#include <QAbstractItemView>
#include <QPushButton>
#include <QLabel>
#include <QPixmap>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QColor>
#include <QShowEvent>
#include <QCloseEvent>
#include <QHideEvent>
#include <QEvent>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMenu>
#include <QCursor>
#include <QMouseEvent>
#include <QTimer>
#include <QSet>
#include <QVector>
#include <QSignalBlocker>
#include <QSettings>

#include <algorithm>
#include <numeric>

namespace
{
	// Swatch icon for a material: its actual albedo/diffuse texture (center-
	// cropped square, scaled to fill) multiplied by the material's tint color
	// - true PBR albedo is baseColorTexture * baseColorFactor (or, for the
	// Specular-Glossiness workflow, diffuseTexture * diffuseColor; same branch
	// meshRepresentativeColor() in MeshColorUtils.cpp uses), so this is what
	// the mesh actually looks like rather than either signal alone. Most of
	// this app's materials are texture-driven, so a flat color alone (the
	// previous behavior) showed nearly every row as a washed-out near-white
	// swatch regardless of how the mesh actually looks. Falls back to a flat
	// tint fill when the material has no texture in that slot (untextured
	// solid-color materials) - deliberately not extended to also depict
	// metalness/roughness with no texture; that's a separate concern from
	// "which color is this."
	// 56px (matching _list's setIconSize() below, not upscaled from a
	// smaller pixmap) gives each row real presence in the list. Also used at
	// a larger size for showHoverPreview()'s preview popup - same
	// compositing, just a bigger edge.
	QPixmap materialSwatchPixmap(const Material& material, int edge)
	{
		const bool useSpecularGlossiness = material.getUseSpecularGlossiness();
		const QVector3D tint = useSpecularGlossiness ? material.diffuseColor() : material.albedoColor();
		const Material::Texture& albedoTex = material.texture(
			useSpecularGlossiness ? Material::TextureType::Diffuse : Material::TextureType::Albedo);
		const QColor tintColor = QColor::fromRgbF(
			std::clamp(tint.x(), 0.0f, 1.0f),
			std::clamp(tint.y(), 0.0f, 1.0f),
			std::clamp(tint.z(), 0.0f, 1.0f));

		QPixmap pixmap(edge, edge);
		pixmap.fill(Qt::transparent);
		{
			QPainter painter(&pixmap);
			painter.setRenderHint(QPainter::Antialiasing);

			QPainterPath clipPath;
			clipPath.addRoundedRect(QRectF(0.5, 0.5, edge - 1.0, edge - 1.0), 6, 6);
			painter.setClipPath(clipPath);

			if (!albedoTex.imageData.isNull())
			{
				const QImage& src = albedoTex.imageData;
				const int cropSize = std::min(src.width(), src.height());
				const QRect cropRect((src.width() - cropSize) / 2, (src.height() - cropSize) / 2, cropSize, cropSize);
				const QImage scaled = src.copy(cropRect).scaled(
					edge, edge, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

				painter.drawImage(0, 0, scaled);
				painter.setCompositionMode(QPainter::CompositionMode_Multiply);
				painter.fillRect(0, 0, edge, edge, tintColor);
				painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
			}
			else
			{
				painter.fillRect(0, 0, edge, edge, tintColor);
			}

			painter.setClipping(false);
			painter.setBrush(Qt::NoBrush);
			painter.setPen(QColor(0, 0, 0, 80));
			painter.drawRoundedRect(QRectF(0.5, 0.5, edge - 1.0, edge - 1.0), 6, 6);
		}
		return pixmap;
	}

	QIcon materialSwatchIcon(const Material& material, int edge = 56)
	{
		return QIcon(materialSwatchPixmap(material, edge));
	}

	// Walks up the parent chain from a widget inside the MDI area to find the QMdiArea itself -
	// same helper as ShrinkWrapDialog.cpp/RtRenderDialog.cpp, redeclared locally per that file's
	// own convention.
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

FilterByMaterialDialog::FilterByMaterialDialog(ModelViewer* modelViewer, QWidget* parent)
	: QDialog(parent)
	, _modelViewer(modelViewer)
{
	setWindowTitle(tr("Filter by Material"));
	resize(420, 480);
	setAttribute(Qt::WA_DeleteOnClose);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(16, 16, 16, 16);
	layout->setSpacing(12);

	auto* introLabel = new QLabel(tr("Select a material to preview every mesh in the scene that "
	                                  "uses it, then Show Only or Hide the result:"), this);
	introLabel->setWordWrap(true);
	layout->addWidget(introLabel);

	_materialsGroup = new QGroupBox(tr("Materials in Scene"), this);
	auto* materialsLayout = new QVBoxLayout(_materialsGroup);

	auto* searchRow = new QHBoxLayout();
	_searchBox = new QLineEdit(_materialsGroup);
	_searchBox->setPlaceholderText(tr("Search materials..."));
	_searchBox->setClearButtonEnabled(true);
	searchRow->addWidget(_searchBox, 1);

	_sortByCountCheck = new QCheckBox(tr("Sort by usage"), _materialsGroup);
	_sortByCountCheck->setToolTip(tr("Sort by mesh count (most used first) instead of alphabetically"));
	searchRow->addWidget(_sortByCountCheck);
	materialsLayout->addLayout(searchRow);

	_list = new QListWidget(_materialsGroup);
	_list->setIconSize(QSize(56, 56));
	_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
	// Mouse tracking on the viewport specifically (not _list itself) - that's
	// the widget that actually receives move events, and eventFilter() below
	// watches it directly for the icon-only hover preview.
	_list->viewport()->setMouseTracking(true);
	_list->viewport()->installEventFilter(this);
	// Policy/signal on _list itself, not its viewport - matches every other
	// list/tree context menu in this codebase (ExplodedViewPanel's captured-
	// views list, SceneTreeWidget, AnimationsPanel/CamerasPanel/
	// MaterialVariantsPanel's trees, etc.); setting it on the viewport
	// instead silently never fired the signal.
	_list->setContextMenuPolicy(Qt::CustomContextMenu);
	materialsLayout->addWidget(_list);

	_hoverTimer = new QTimer(this);
	_hoverTimer->setSingleShot(true);
	_hoverTimer->setInterval(500);

	layout->addWidget(_materialsGroup, 1);

	auto* buttonRow = new QHBoxLayout();
	buttonRow->addStretch();
	_showOnlyButton = new QPushButton(tr("Show Only"), this);
	_hideButton = new QPushButton(tr("Hide"), this);
	buttonRow->addWidget(_showOnlyButton);
	buttonRow->addWidget(_hideButton);
	layout->addLayout(buttonRow);

	connect(_list, &QListWidget::itemSelectionChanged, this, &FilterByMaterialDialog::onRowChanged);
	connect(_list, &QListWidget::itemDoubleClicked, this, &FilterByMaterialDialog::onItemDoubleClicked);
	connect(_hoverTimer, &QTimer::timeout, this, &FilterByMaterialDialog::showHoverPreview);
	connect(_list, &QWidget::customContextMenuRequested, this, &FilterByMaterialDialog::onListContextMenuRequested);
	connect(_searchBox, &QLineEdit::textChanged, this, &FilterByMaterialDialog::onFilterTextChanged);
	connect(_sortByCountCheck, &QCheckBox::toggled, this, &FilterByMaterialDialog::rebuildGroups);
	connect(_showOnlyButton, &QPushButton::clicked, this, &FilterByMaterialDialog::onShowOnlyClicked);
	connect(_hideButton, &QPushButton::clicked, this, &FilterByMaterialDialog::onHideClicked);

	// Hide/show this dialog as its OWN document's MDI subwindow loses/gains focus - mirrors
	// ShrinkWrapDialog's identical mechanism. Without this, a dialog opened for one document kept
	// showing (and still live-previewing) that document's stale state even while a different one
	// became the active tab.
	if (_modelViewer)
	{
		if (QMdiArea* mdiArea = findMdiArea(_modelViewer))
			connect(mdiArea, &QMdiArea::subWindowActivated, this, &FilterByMaterialDialog::onActiveSubWindowChanged);
	}

	loadSettings();
	rebuildGroups();
}

void FilterByMaterialDialog::showEvent(QShowEvent* event)
{
	QDialog::showEvent(event);
	// The scene's materials (or the mesh store itself) may have changed
	// while this dialog was hidden behind another document tab - rebuild
	// from scratch rather than trusting the snapshot taken at construction
	// or the last time it was shown.
	rebuildGroups();
}

void FilterByMaterialDialog::changeEvent(QEvent* event)
{
	QDialog::changeEvent(event);
	if (event->type() == QEvent::ActivationChange && isActiveWindow())
		applyLiveSelection();
}

void FilterByMaterialDialog::closeEvent(QCloseEvent* event)
{
	saveSettings();
	QDialog::closeEvent(event);
}

void FilterByMaterialDialog::reject()
{
	saveSettings();
	QDialog::reject();
}

void FilterByMaterialDialog::loadSettings()
{
	QSettings settings;
	const QByteArray geometry = settings.value("filterByMaterial/geometry", QByteArray()).toByteArray();
	if (!geometry.isEmpty())
		restoreGeometry(geometry);

	// Blocked: this runs before rebuildGroups()'s first call in the
	// constructor, so toggled() firing here would just re-enter a
	// rebuildGroups() that's about to run anyway from the ctor's own
	// explicit call right after loadSettings().
	const QSignalBlocker blocker(_sortByCountCheck);
	_sortByCountCheck->setChecked(settings.value("filterByMaterial/sortByCount", false).toBool());
}

void FilterByMaterialDialog::saveSettings()
{
	QSettings settings;
	settings.setValue("filterByMaterial/geometry", saveGeometry());
	settings.setValue("filterByMaterial/sortByCount", _sortByCountCheck->isChecked());
}

void FilterByMaterialDialog::hideEvent(QHideEvent* event)
{
	QDialog::hideEvent(event);
	if (_hoverPreview)
		_hoverPreview->hide();
}

void FilterByMaterialDialog::onActiveSubWindowChanged(QMdiSubWindow* activeSubWindow)
{
	const bool isOwnDocumentActive = _modelViewer
		&& activeSubWindow
		&& activeSubWindow->widget() == static_cast<QWidget*>(_modelViewer);
	setVisible(isOwnDocumentActive);
}

void FilterByMaterialDialog::rebuildGroups()
{
	if (!_modelViewer || !_modelViewer->getViewportWidget())
		return;

	const QString previousSearch = _searchBox ? _searchBox->text() : QString();
	// Preserve the selected rows across a rebuild (e.g. the document tab was
	// switched away and back, or an unrelated scene edit happened) by their
	// label text - clearing the list invalidates every QListWidgetItem*, so
	// without this every rebuild would silently snap the live selection back
	// to nothing selected.
	QSet<QString> previouslySelectedLabels;
	for (QListWidgetItem* item : _list->selectedItems())
		previouslySelectedLabels.insert(item->text());

	std::vector<SceneMesh*> meshStore = _modelViewer->getViewportWidget()->getMeshStore();
	QVector<SceneMesh*> meshes(meshStore.cbegin(), meshStore.cend());

	// Deliberately groupIndicesByCurrentMaterial(), NOT groupIndicesByMaterial() - this
	// dialog answers "what does this mesh currently look like," not "where did it come
	// from at import" (see MaterialGrouping.h's doc comment for the confirmed bug this
	// avoids: a mesh re-materialed via the eyedropper/Apply keeps its original import
	// identity, which is the wrong grouping for a material filter).
	std::vector<int> allIndices(meshes.size());
	std::iota(allIndices.begin(), allIndices.end(), 0);
	_groups = groupIndicesByCurrentMaterial(meshes, allIndices);

	// Collected first, then sorted, rather than inserting into _list and
	// calling its own sortItems() - gives full control over the "by usage"
	// order below, which QListWidget's built-in text-based sort can't do.
	struct RowInfo
	{
		QString label;
		QIcon icon;
		int groupIndex;
		std::size_t meshCount;
	};
	std::vector<RowInfo> rows;
	rows.reserve(_groups.size());
	for (std::size_t g = 0; g < _groups.size(); ++g)
	{
		const std::vector<int>& group = _groups[g];
		if (group.empty())
			continue;

		SceneMesh* refMesh = meshes[group[0]];
		QString name = refMesh->getMaterial().name();
		if (name.isEmpty())
			name = tr("Unnamed Material");

		const QString label = group.size() == 1
			? tr("%1 (1 mesh)").arg(name)
			: tr("%1 (%2 meshes)").arg(name).arg(group.size());

		rows.push_back({ label, materialSwatchIcon(refMesh->getMaterial()), static_cast<int>(g), group.size() });
	}

	if (_sortByCountCheck->isChecked())
	{
		std::stable_sort(rows.begin(), rows.end(), [](const RowInfo& a, const RowInfo& b) {
			return a.meshCount > b.meshCount;
		});
	}
	else
	{
		std::stable_sort(rows.begin(), rows.end(), [](const RowInfo& a, const RowInfo& b) {
			return QString::compare(a.label, b.label, Qt::CaseInsensitive) < 0;
		});
	}

	// _list->clear() deletes every QListWidgetItem - drop any pending/shown
	// hover preview first so _hoverArmedItem never dangles (a pending
	// _hoverTimer firing afterward would otherwise dereference a deleted
	// item in showHoverPreview()).
	_hoverArmedItem = nullptr;
	_hoverTimer->stop();
	if (_hoverPreview)
		_hoverPreview->hide();

	_list->clear();
	for (const RowInfo& row : rows)
	{
		auto* item = new QListWidgetItem(row.icon, row.label, _list);
		item->setData(Qt::UserRole, row.groupIndex);
	}

	updateGroupBoxTitle();
	if (!previousSearch.isEmpty())
	{
		_searchBox->setText(previousSearch);
		onFilterTextChanged(previousSearch);
	}

	// Deliberately no "select row 0 by default" fallback: on first open
	// (previouslySelectedLabels empty, nothing to restore) this leaves the
	// list with nothing selected, so opening the dialog never silently
	// replaces whatever the user already had selected in the viewport or
	// pushes an undo entry before they've chosen anything. Same reasoning
	// applies to a rebuild - if the user deliberately cleared the list
	// selection (Ctrl-click to deselect all) before switching tabs away and
	// back, that "nothing selected" state is itself preserved, not treated
	// as something to fall back away from.
	if (!previouslySelectedLabels.isEmpty())
	{
		// Block signals while re-selecting each match so onRowChanged() (and
		// the live-selection push it triggers) only runs once, after the
		// full set is restored, not once per item.
		const QSignalBlocker blocker(_list);
		for (int i = 0; i < _list->count(); ++i)
		{
			QListWidgetItem* item = _list->item(i);
			if (previouslySelectedLabels.contains(item->text()))
				item->setSelected(true);
		}
	}

	onRowChanged();
}

void FilterByMaterialDialog::updateGroupBoxTitle()
{
	if (!_materialsGroup || !_list)
		return;

	const int total = _list->count();
	int visible = 0;
	for (int i = 0; i < total; ++i)
	{
		if (!_list->item(i)->isHidden())
			++visible;
	}

	_materialsGroup->setTitle(visible == total
		? tr("Materials in Scene (%1)").arg(total)
		: tr("Materials in Scene (%1 of %2)").arg(visible).arg(total));
}

std::vector<int> FilterByMaterialDialog::selectedGroups() const
{
	// A QSet first, since two selected rows can never share a mesh index
	// (groupIndicesByCurrentMaterial() partitions the scene) but this keeps
	// the union correct even if that ever changed, and gets de-duplication
	// for free.
	QSet<int> union_;
	for (QListWidgetItem* item : _list->selectedItems())
	{
		const int groupIndex = item->data(Qt::UserRole).toInt();
		if (groupIndex >= 0 && groupIndex < static_cast<int>(_groups.size()))
		{
			const std::vector<int>& group = _groups[groupIndex];
			union_.unite(QSet<int>(group.cbegin(), group.cend()));
		}
	}
	return std::vector<int>(union_.cbegin(), union_.cend());
}

void FilterByMaterialDialog::applyLiveSelection()
{
	if (!_modelViewer)
		return;

	// No row picked yet (fresh open, or the user deliberately Ctrl-clicked
	// to clear the list selection) - unlike FilterByColorDialog, an empty
	// row selection here isn't a real "zero matches" query result, it's
	// "nothing chosen." Leave the live viewport selection exactly as it
	// was rather than forcing it to empty, so opening this dialog (or
	// clearing its selection) never clobbers whatever the user already had
	// selected before touching it.
	if (_list->selectedItems().isEmpty())
		return;

	const std::vector<int> group = selectedGroups();
	const QSet<int> newSelection(group.cbegin(), group.cend());

	const std::vector<int> currentIds = _modelViewer->getSelectedIDs();
	const QSet<int> currentSelection(currentIds.cbegin(), currentIds.cend());
	if (newSelection == currentSelection)
		return;

	// mergeSource == this: consecutive row clicks while this dialog stays
	// open collapse into one undo step (see SelectionCommand's mergeWith()).
	_modelViewer->setSelectionWithUndo(newSelection, this);
}

void FilterByMaterialDialog::onRowChanged()
{
	const bool hasSelection = !_list->selectedItems().isEmpty();
	_showOnlyButton->setEnabled(hasSelection);
	_hideButton->setEnabled(hasSelection);
	applyLiveSelection();
}

void FilterByMaterialDialog::onFilterTextChanged(const QString& text)
{
	for (int i = 0; i < _list->count(); ++i)
	{
		QListWidgetItem* item = _list->item(i);
		item->setHidden(!text.isEmpty() && !item->text().contains(text, Qt::CaseInsensitive));
	}
	updateGroupBoxTitle();
}

void FilterByMaterialDialog::onItemDoubleClicked(QListWidgetItem* /*item*/)
{
	// Single click already live-previews the selection - double-click is a
	// shortcut straight to the most common follow-up action.
	onShowOnlyClicked();
}

void FilterByMaterialDialog::onShowOnlyClicked()
{
	if (_modelViewer)
		_modelViewer->showOnlySelectedItems();
}

void FilterByMaterialDialog::onHideClicked()
{
	if (_modelViewer)
		_modelViewer->hideSelectedItems();
}

QRect FilterByMaterialDialog::iconRectForItem(QListWidgetItem* item) const
{
	if (!item)
		return QRect();
	return QRect(_list->visualItemRect(item).topLeft(), _list->iconSize());
}

bool FilterByMaterialDialog::eventFilter(QObject* watched, QEvent* event)
{
	// Raw mouse-move tracking instead of QListWidget::itemEntered() -
	// itemEntered() only reports row transitions (icon vs. text within the
	// same row is invisible to it), and only fires once per row rather than
	// letting a hover dwell before showing anything. Both are required here:
	// the preview should arm only over the swatch icon, and only after a
	// deliberate pause, not an instant flash while scanning down the list.
	if (watched == _list->viewport())
	{
		if (event->type() == QEvent::MouseMove)
		{
			auto* mouseEvent = static_cast<QMouseEvent*>(event);
			QListWidgetItem* item = _list->itemAt(mouseEvent->pos());
			QListWidgetItem* iconItem = (item && iconRectForItem(item).contains(mouseEvent->pos())) ? item : nullptr;
			if (iconItem != _hoverArmedItem)
			{
				_hoverArmedItem = iconItem;
				_hoverTimer->stop();
				if (_hoverPreview)
					_hoverPreview->hide();
				if (iconItem)
					_hoverTimer->start();
			}
		}
		else if (event->type() == QEvent::Leave)
		{
			_hoverArmedItem = nullptr;
			_hoverTimer->stop();
			if (_hoverPreview)
				_hoverPreview->hide();
		}
	}
	return QDialog::eventFilter(watched, event);
}

void FilterByMaterialDialog::showHoverPreview()
{
	if (!_hoverArmedItem || !_modelViewer || !_modelViewer->getViewportWidget())
		return;

	const int groupIndex = _hoverArmedItem->data(Qt::UserRole).toInt();
	if (groupIndex < 0 || groupIndex >= static_cast<int>(_groups.size()) || _groups[groupIndex].empty())
		return;

	// Recomputed on demand from the group's reference mesh rather than
	// cached per-item at rebuild time - only costs a texture crop/scale once
	// the long-hover delay actually elapses, and avoids holding a 160x160
	// pixmap per row in memory for the common case where the user never
	// dwells on most of them.
	const std::vector<SceneMesh*> meshStore = _modelViewer->getViewportWidget()->getMeshStore();
	const int meshIndex = _groups[groupIndex][0];
	if (meshIndex < 0 || meshIndex >= static_cast<int>(meshStore.size()))
		return;

	if (!_hoverPreview)
	{
		// Parented to `this` (destroyed with the dialog) but still a
		// top-level Qt::ToolTip window - Qt supports both at once, which is
		// exactly the tooltip-that-follows-a-parent's-lifetime shape this
		// needs. WA_ShowWithoutActivating keeps focus on the list/dialog.
		_hoverPreview = new QLabel(this, Qt::ToolTip | Qt::FramelessWindowHint);
		_hoverPreview->setAttribute(Qt::WA_ShowWithoutActivating);
		_hoverPreview->setStyleSheet(
			"QLabel { background-color: #2a2a2a; border: 1px solid #555555; "
			"padding: 6px; border-radius: 4px; }");
	}
	_hoverPreview->setPixmap(materialSwatchPixmap(meshStore[meshIndex]->getMaterial(), 160));
	_hoverPreview->adjustSize();
	_hoverPreview->move(QCursor::pos() + QPoint(18, 18));
	_hoverPreview->show();
}

void FilterByMaterialDialog::onListContextMenuRequested(const QPoint& pos)
{
	QListWidgetItem* item = _list->itemAt(pos);
	if (!item || !_modelViewer)
		return;

	// Right-clicking a row outside the current selection jumps the selection
	// to just that row first, same "right-click acts on what you clicked"
	// convention as the Scene Tree/viewport context menus elsewhere in this
	// app - setSelected() fires itemSelectionChanged() synchronously, so by
	// the time editMeshMaterial() runs below, the live viewport selection
	// (and therefore what Edit Material operates on) already matches.
	if (!item->isSelected())
	{
		_list->clearSelection();
		item->setSelected(true);
	}

	QMenu menu(this);
	// Only entry for now - Show Only/Hide already have their own dedicated
	// buttons, and this dialog otherwise has no per-material actions beyond
	// jumping into the Material Properties panel to actually change
	// something about it.
	QAction* editAction = menu.addAction(tr("Edit Material..."));
	if (menu.exec(_list->viewport()->mapToGlobal(pos)) == editAction)
		_modelViewer->editMeshMaterial();
}
