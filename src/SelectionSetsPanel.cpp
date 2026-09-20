#include "SelectionSetsPanel.h"
#include "LanguageManager.h"
#include "SceneGraph.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QInputDialog>
#include <QLineEdit>
#include <QLabel>

SelectionSetsPanel::SelectionSetsPanel(QWidget* parent)
	: QWidget(parent)
{
	auto* layout = new QVBoxLayout(this);

	_headerLabel = new QLabel(tr("Saved selections for this document:"), this);
	_headerLabel->setWordWrap(true);   // a long translation wraps instead of widening the dock
	layout->addWidget(_headerLabel);

	_list = new QListWidget(this);
	layout->addWidget(_list, 1);

	auto* buttonRow = new QHBoxLayout();
	_saveButton = new QPushButton(tr("Save Current Selection..."), this);
	_deleteButton = new QPushButton(tr("Delete"), this);
	_deleteButton->setEnabled(false);
	buttonRow->addWidget(_saveButton);
	buttonRow->addWidget(_deleteButton);
	buttonRow->addStretch();
	layout->addLayout(buttonRow);

	connect(_list, &QListWidget::itemClicked, this, &SelectionSetsPanel::onItemClicked);
	connect(_list, &QListWidget::itemSelectionChanged, this, &SelectionSetsPanel::onSelectionChanged);
	connect(_saveButton, &QPushButton::clicked, this, &SelectionSetsPanel::onSaveButtonClicked);
	connect(_deleteButton, &QPushButton::clicked, this, &SelectionSetsPanel::onDeleteButtonClicked);
	connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, &SelectionSetsPanel::retranslateUI);
}

void SelectionSetsPanel::retranslateUI()
{
	_headerLabel->setText(tr("Saved selections for this document:"));
	_saveButton->setText(tr("Save Current Selection..."));
	_deleteButton->setText(tr("Delete"));
	refresh();   // row labels are built with tr() too
}

void SelectionSetsPanel::setSceneGraph(SceneGraph* sg)
{
	_sceneGraph = sg;
}

void SelectionSetsPanel::refresh()
{
	_list->clear();

	if (_sceneGraph)
	{
		for (const SelectionSet& set : _sceneGraph->selectionSets())
		{
			const QString label = set.meshUuids.size() == 1
				? tr("%1 (1 mesh)").arg(set.name)
				: tr("%1 (%2 meshes)").arg(set.name).arg(set.meshUuids.size());
			auto* item = new QListWidgetItem(label, _list);
			item->setData(Qt::UserRole, set.id.toString());
		}
	}

	// Rows were just rebuilt from scratch - re-derive and re-apply whatever
	// active-set highlight was last known, rather than leaving every row
	// unhighlighted until the next unrelated selection change.
	updateActiveHighlight();
	onSelectionChanged();
}

void SelectionSetsPanel::syncActiveSet(const QSet<QUuid>& currentSelectionUuids)
{
	_lastKnownSelectionUuids = currentSelectionUuids;
	updateActiveHighlight();
}

void SelectionSetsPanel::updateActiveHighlight()
{
	_activeSetId = QUuid();
	if (_sceneGraph && !_lastKnownSelectionUuids.isEmpty())
	{
		for (const SelectionSet& set : _sceneGraph->selectionSets())
		{
			if (set.meshUuids == _lastKnownSelectionUuids)
			{
				_activeSetId = set.id;
				break;
			}
		}
	}

	QListWidgetItem* matchingItem = nullptr;
	if (!_activeSetId.isNull())
	{
		for (int i = 0; i < _list->count() && !matchingItem; ++i)
		{
			QListWidgetItem* item = _list->item(i);
			if (QUuid(item->data(Qt::UserRole).toString()) == _activeSetId)
				matchingItem = item;
		}
	}

	// setCurrentItem()/clearSelection() are programmatic - neither emits
	// itemClicked, so this never loops back into onItemClicked() and
	// re-triggers a recall/deselect.
	if (matchingItem)
		_list->setCurrentItem(matchingItem);
	else
		_list->clearSelection();

	onSelectionChanged();
}

void SelectionSetsPanel::onItemClicked(QListWidgetItem* item)
{
	if (!item)
		return;
	const QUuid id(item->data(Qt::UserRole).toString());
	if (id.isNull())
		return;

	// Clicking the already-active row again toggles it off (deselect)
	// instead of recalling the same selection it's already showing.
	if (id == _activeSetId)
		emit selectionSetDeselectRequested();
	else
		emit selectionSetRecallRequested(id);
}

void SelectionSetsPanel::onSaveButtonClicked()
{
	bool ok = false;
	const QString name = QInputDialog::getText(this, tr("Save Selection Set"),
		tr("Name for this selection:"), QLineEdit::Normal, QString(), &ok);
	if (!ok || name.trimmed().isEmpty())
		return;
	emit selectionSetSaveRequested(name.trimmed());
}

void SelectionSetsPanel::onDeleteButtonClicked()
{
	QListWidgetItem* item = _list->currentItem();
	if (!item)
		return;
	const QUuid id(item->data(Qt::UserRole).toString());
	if (!id.isNull())
		emit selectionSetDeleteRequested(id);
}

void SelectionSetsPanel::onSelectionChanged()
{
	_deleteButton->setEnabled(_list->currentItem() != nullptr);
}
