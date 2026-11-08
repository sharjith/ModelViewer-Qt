#include "MeshInfoDialog.h"
#include "ModelViewer.h"
#include "ViewportWidget.h"
#include "SceneMesh.h"
#include "MeshSelectionBox.h"
#include "NodeProperties.h"
#include "Material.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QVBoxLayout>
#include <unordered_map>
#include <vector>

namespace
{
	// Largest number of table rows built; a selection spanning very many nodes is cut here and says so.
	constexpr int MaxTableRows = 20000;
	// Per-mesh statistics are listed up to this many meshes; beyond it only the selection totals are shown.
	constexpr int MaxMeshesWithOwnRows = 100;

	QString memoryText(unsigned long long bytes)
	{
		if (bytes < 1024ull)
			return QObject::tr("%1 bytes").arg(bytes);
		if (bytes < 1024ull * 1024)
			return QObject::tr("%1 kb").arg(bytes / 1024);
		if (bytes < 1024ull * 1024 * 1024)
			return QObject::tr("%1 mb").arg(bytes / (1024 * 1024));
		return QObject::tr("%1 gb").arg(bytes / (1024ull * 1024 * 1024));
	}

	struct InfoRow
	{
		QString mesh, source, property, value, type;
	};

	QMdiArea* findMdiArea(QWidget* widget)
	{
		for (QWidget* w = widget; w; w = w->parentWidget())
		{
			if (auto* area = qobject_cast<QMdiArea*>(w))
				return area;
		}
		return nullptr;
	}

	struct SetUse
	{
		NodePropertySetPtr set;
		QString firstMesh;
		int meshCount = 0;
		int level = 0; // 0 = the mesh's own node, 1 = nearest ancestor that has properties, 2 = the next ...
	};
}

MeshInfoDialog::MeshInfoDialog(ModelViewer* modelViewer, QWidget* parent)
	: QDialog(parent)
	, _modelViewer(modelViewer)
{
	setWindowTitle(tr("Mesh Info"));
	resize(720, 460);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(16, 16, 16, 16);
	layout->setSpacing(12);

	auto* introLabel = new QLabel(tr("Statistics for the selected meshes, then the source properties kept from the imported "
	                                  "file for their nodes - read-only, values exactly as stored (no unit conversion)."), this);
	introLabel->setWordWrap(true);
	introLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
	layout->addWidget(introLabel);

	_selectionBox = new MeshSelectionBox(_modelViewer, this);
	_selectionBox->setFieldToolTip(tr("The meshes this table describes. Right-click to edit or clear."));
	_selectionBox->setEditorTexts(tr("Review and refine the meshes in this list."), tr("Meshes"));
	layout->addWidget(_selectionBox);

	_summaryLabel = new QLabel(this);
	_summaryLabel->setWordWrap(true);
	_summaryLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
	layout->addWidget(_summaryLabel);

	_noSelectionLabel = new QLabel(tr("Nothing selected - select one or more meshes first."), this);
	_noSelectionLabel->setWordWrap(true);
	_noSelectionLabel->setAlignment(Qt::AlignCenter);
	_noSelectionLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
	layout->addWidget(_noSelectionLabel, 1);

	_searchEdit = new QLineEdit(this);
	_searchEdit->setPlaceholderText(tr("Search meshes, properties or values..."));
	_searchEdit->setClearButtonEnabled(true);
	connect(_searchEdit, &QLineEdit::textChanged, this, &MeshInfoDialog::applySearchFilter);
	layout->addWidget(_searchEdit);

	_table = new QTableWidget(this);
	_table->setColumnCount(5);
	_table->setHorizontalHeaderLabels({ tr("Mesh"), tr("Node"), tr("Property"), tr("Value"), tr("Type") });
	_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
	_table->horizontalHeader()->setStretchLastSection(true);
	_table->horizontalHeader()->setMinimumSectionSize(60);
	_table->setColumnWidth(0, 130);
	_table->setColumnWidth(1, 130);
	_table->setColumnWidth(2, 170);
	_table->setColumnWidth(3, 220);
	_table->verticalHeader()->setVisible(false);
	_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
	_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	_table->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(_table, &QWidget::customContextMenuRequested, this, &MeshInfoDialog::showTableContextMenu);
	layout->addWidget(_table, 1);

	_refreshButton = new QPushButton(tr("Refresh"), this);
	_refreshButton->setToolTip(tr("Rebuild the table from the current mesh list"));
	connect(_refreshButton, &QPushButton::clicked, this, [this]() { populate(); });
	_closeButton = new QPushButton(tr("Close"), this);
	connect(_closeButton, &QPushButton::clicked, this, &QWidget::close);
	auto* buttonRow = new QHBoxLayout();
	buttonRow->addStretch(1);
	buttonRow->addWidget(_refreshButton);
	buttonRow->addWidget(_closeButton);
	layout->addLayout(buttonRow);

	// Seed before connecting so opening the dialog builds the table once.
	_selectionBox->seedFromViewportSelection();
	connect(_selectionBox, &MeshSelectionBox::meshUuidsChanged, this, [this]() { populate(); });

	// Follow the owning document's MDI subwindow, as the other non-modal tool dialogs do.
	if (_modelViewer)
	{
		if (QMdiArea* mdiArea = findMdiArea(_modelViewer))
			connect(mdiArea, &QMdiArea::subWindowActivated, this, &MeshInfoDialog::onActiveSubWindowChanged);
	}

	populate();
	loadSettings();
}

void MeshInfoDialog::seedFromViewportSelection()
{
	_selectionBox->seedFromViewportSelection(); // rebuilds through meshUuidsChanged()
}

void MeshInfoDialog::onActiveSubWindowChanged(QMdiSubWindow* activeSubWindow)
{
	const bool isOwnDocumentActive = _modelViewer
		&& activeSubWindow
		&& activeSubWindow->widget() == static_cast<QWidget*>(_modelViewer);
	setVisible(isOwnDocumentActive);
}

void MeshInfoDialog::applySearchFilter()
{
	const QString needle = _searchEdit->text().trimmed();
	for (int row = 0; row < _table->rowCount(); ++row)
	{
		bool visible = needle.isEmpty();
		for (int column = 0; !visible && column < 4; ++column)
		{
			const QTableWidgetItem* item = _table->item(row, column);
			visible = item && item->text().contains(needle, Qt::CaseInsensitive);
		}
		_table->setRowHidden(row, !visible);
	}
}

void MeshInfoDialog::showTableContextMenu(const QPoint& pos)
{
	const QModelIndexList rows = _table->selectionModel()->selectedRows();
	if (rows.isEmpty())
		return;
	QMenu menu(this);
	QAction* copy = menu.addAction(tr("Copy rows"));
	if (menu.exec(_table->viewport()->mapToGlobal(pos)) != copy)
		return;
	QStringList lines;
	for (const QModelIndex& index : rows)
	{
		QStringList cells;
		for (int column = 0; column < _table->columnCount(); ++column)
		{
			const QTableWidgetItem* item = _table->item(index.row(), column);
			cells << (item ? item->text() : QString());
		}
		lines << cells.join(QLatin1Char('\t'));
	}
	QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
}

void MeshInfoDialog::populate()
{
	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	if (!viewport)
		return;

	const std::vector<SceneMesh*> meshStore = viewport->getMeshStore();
	std::vector<SetUse> uses;
	std::unordered_map<const NodePropertySet*, size_t> useIndex;
	std::vector<const SceneMesh*> selected;
	int meshesWithout = 0;
	for (const QUuid& uuid : _selectionBox->meshUuids())
	{
		const int id = viewport->getIndexByUuid(uuid);
		if (id < 0 || size_t(id) >= meshStore.size() || !meshStore[size_t(id)])
			continue; // deleted since it was added
		const SceneMesh* mesh = meshStore[size_t(id)];
		selected.push_back(mesh);
		const NodePropertyChainPtr& chain = mesh->getNodeProperties();
		if (!chain || chain->empty())
		{
			++meshesWithout;
			continue;
		}
		for (size_t level = 0; level < chain->size(); ++level)
		{
			const NodePropertySetPtr& set = (*chain)[level];
			auto found = useIndex.find(set.get());
			if (found == useIndex.end())
			{
				found = useIndex.emplace(set.get(), uses.size()).first;
				uses.push_back({ set, mesh->getName(), 0, int(level) });
			}
			++uses[found->second].meshCount;
		}
	}

	const int meshes = int(selected.size());
	_noSelectionLabel->setVisible(meshes == 0);
	_searchEdit->setVisible(meshes != 0);
	_table->setVisible(meshes != 0);
	_summaryLabel->setVisible(meshes != 0);
	_table->clearContents();
	_table->setRowCount(0);
	if (meshes == 0)
		return;

	std::vector<InfoRow> rows;
	auto add = [&rows](const QString& mesh, const QString& source, const QString& property, const QString& value, const QString& type) {
		rows.push_back({ mesh, source, property, value, type });
	};
	const QString meshSource = tr("Mesh");
	size_t totalPoints = 0, totalTriangles = 0;
	unsigned long long totalMemory = 0;
	for (const SceneMesh* mesh : selected)
	{
		const size_t points = mesh->pointCount();
		const size_t triangles = mesh->getIndices().size() / 3;
		const unsigned long long memory = mesh->memorySize();
		totalPoints += points;
		totalTriangles += triangles;
		totalMemory += memory;
		if (meshes > MaxMeshesWithOwnRows)
			continue;
		const QString name = mesh->getName();
		add(name, meshSource, tr("Points"), QString::number(points), QStringLiteral("integer"));
		add(name, meshSource, tr("Triangles"), QString::number(triangles), QStringLiteral("integer"));
		add(name, meshSource, tr("Memory"), memoryText(memory), QStringLiteral("text"));
		add(name, meshSource, tr("Material"), mesh->materialRef().name(), QStringLiteral("text"));
		if (!mesh->getSourceFile().isEmpty())
			add(name, meshSource, tr("Source file"), mesh->getSourceFile(), QStringLiteral("text"));
		if (!mesh->getSourceNodeName().isEmpty())
			add(name, meshSource, tr("Source node"), mesh->getSourceNodeName(), QStringLiteral("text"));
		if (mesh->hasDeclaredMass())
			add(name, meshSource, tr("Declared mass (kg)"), QString::number(mesh->declaredMassKg(), 'g', 10), QStringLiteral("real"));
		if (mesh->hasDeclaredMaterial())
			add(name, meshSource, tr("Declared material"), mesh->declaredMaterialName(), QStringLiteral("text"));
	}
	if (meshes > 1)
	{
		const QString label = tr("%1 meshes").arg(meshes);
		const QString totals = tr("Selection");
		add(label, totals, tr("Points"), QString::number(totalPoints), QStringLiteral("integer"));
		add(label, totals, tr("Triangles"), QString::number(totalTriangles), QStringLiteral("integer"));
		add(label, totals, tr("Memory"), memoryText(totalMemory), QStringLiteral("text"));
	}
	for (const SetUse& use : uses)
	{
		const QString meshText = use.meshCount > 1 ? tr("%1 (+%2 more)").arg(use.firstMesh).arg(use.meshCount - 1) : use.firstMesh;
		const QString nodeText = use.level > 0 ? tr("%1 (ancestor %2)").arg(use.set->nodeName).arg(use.level) : use.set->nodeName;
		for (const NodePropertyRow& property : use.set->rows)
			add(meshText, nodeText, property.key, property.value, property.type);
	}

	const bool cut = int(rows.size()) > MaxTableRows;
	const int count = cut ? MaxTableRows : int(rows.size());
	_table->setRowCount(count);
	for (int row = 0; row < count; ++row)
	{
		const InfoRow& r = rows[size_t(row)];
		_table->setItem(row, 0, new QTableWidgetItem(r.mesh));
		_table->setItem(row, 1, new QTableWidgetItem(r.source));
		_table->setItem(row, 2, new QTableWidgetItem(r.property));
		auto* valueItem = new QTableWidgetItem(r.value);
		valueItem->setToolTip(r.value); // the full text, whatever the column width
		_table->setItem(row, 3, valueItem);
		_table->setItem(row, 4, new QTableWidgetItem(r.type));
	}

	QString summary = tr("%n mesh(es) selected; %1 without source properties.", nullptr, meshes).arg(meshesWithout);
	if (meshes > MaxMeshesWithOwnRows)
		summary += tr(" Per-mesh statistics are listed for up to %1 meshes; totals only here.").arg(MaxMeshesWithOwnRows);
	if (cut)
		summary += tr(" Only the first %1 rows are shown.").arg(MaxTableRows);
	_summaryLabel->setText(summary);
	applySearchFilter();
}

void MeshInfoDialog::closeEvent(QCloseEvent* event)
{
	saveSettings();
	QDialog::closeEvent(event);
}

void MeshInfoDialog::reject()
{
	saveSettings();
	QDialog::reject();
}

void MeshInfoDialog::loadSettings()
{
	QSettings settings;
	const QByteArray geometry = settings.value("meshInfo/geometry", QByteArray()).toByteArray();
	if (!geometry.isEmpty())
		restoreGeometry(geometry);
}

void MeshInfoDialog::saveSettings()
{
	QSettings settings;
	settings.setValue("meshInfo/geometry", saveGeometry());
}
