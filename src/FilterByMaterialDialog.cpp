#include "FilterByMaterialDialog.h"
#include "MaterialGrouping.h"
#include "ModelViewer.h"
#include "ViewportWidget.h"
#include "SceneMesh.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QListWidget>
#include <QAbstractItemView>
#include <QPushButton>
#include <QLabel>
#include <QPixmap>
#include <QPainter>
#include <QColor>
#include <QShowEvent>
#include <QCloseEvent>
#include <QEvent>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QSet>
#include <QVector>
#include <QSignalBlocker>
#include <QSettings>

#include <algorithm>
#include <numeric>

namespace
{
	// Flat-color swatch icon for a material's representative albedo color -
	// a material library/preview widget elsewhere in this app builds richer
	// texture-thumbnail icons, but those are private to that class and
	// texture-file-based; this dialog only ever needs a flat scalar swatch.
	// 40px (matching _list's setIconSize() below, not upscaled from a
	// smaller pixmap) gives each row real presence in the list.
	QIcon colorSwatchIcon(const QVector3D& albedo, int edge = 40)
	{
		QPixmap pixmap(edge, edge);
		pixmap.fill(Qt::transparent);
		QPainter painter(&pixmap);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setPen(QColor(0, 0, 0, 80));
		painter.setBrush(QColor::fromRgbF(
			std::clamp(albedo.x(), 0.0f, 1.0f),
			std::clamp(albedo.y(), 0.0f, 1.0f),
			std::clamp(albedo.z(), 0.0f, 1.0f)));
		painter.drawRoundedRect(1, 1, edge - 2, edge - 2, 6, 6);
		return QIcon(pixmap);
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

	auto* materialsGroup = new QGroupBox(tr("Materials in Scene"), this);
	auto* materialsLayout = new QVBoxLayout(materialsGroup);

	_searchBox = new QLineEdit(materialsGroup);
	_searchBox->setPlaceholderText(tr("Search materials..."));
	_searchBox->setClearButtonEnabled(true);
	materialsLayout->addWidget(_searchBox);

	_list = new QListWidget(materialsGroup);
	_list->setIconSize(QSize(40, 40));
	_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
	materialsLayout->addWidget(_list);

	layout->addWidget(materialsGroup, 1);

	auto* buttonRow = new QHBoxLayout();
	buttonRow->addStretch();
	_showOnlyButton = new QPushButton(tr("Show Only"), this);
	_hideButton = new QPushButton(tr("Hide"), this);
	buttonRow->addWidget(_showOnlyButton);
	buttonRow->addWidget(_hideButton);
	layout->addLayout(buttonRow);

	connect(_list, &QListWidget::itemSelectionChanged, this, &FilterByMaterialDialog::onRowChanged);
	connect(_list, &QListWidget::itemDoubleClicked, this, &FilterByMaterialDialog::onItemDoubleClicked);
	connect(_searchBox, &QLineEdit::textChanged, this, &FilterByMaterialDialog::onFilterTextChanged);
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
}

void FilterByMaterialDialog::saveSettings()
{
	QSettings settings;
	settings.setValue("filterByMaterial/geometry", saveGeometry());
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

	_list->clear();
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

		auto* item = new QListWidgetItem(colorSwatchIcon(refMesh->getMaterial().albedoColor()), label, _list);
		item->setData(Qt::UserRole, static_cast<int>(g));
	}

	_list->sortItems();
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
