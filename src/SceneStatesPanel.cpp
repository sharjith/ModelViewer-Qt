#include "SceneStatesPanel.h"
#include "SceneGraph.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QInputDialog>
#include <QLineEdit>
#include <QLabel>

SceneStatesPanel::SceneStatesPanel(QWidget* parent)
	: QWidget(parent)
{
	auto* layout = new QVBoxLayout(this);

	layout->addWidget(new QLabel(tr("Saved scene states for this document:"), this));

	_list = new QListWidget(this);
	layout->addWidget(_list, 1);

	auto* buttonRow = new QHBoxLayout();
	_saveButton = new QPushButton(tr("Save Current State..."), this);
	_deleteButton = new QPushButton(tr("Delete"), this);
	_deleteButton->setEnabled(false);
	buttonRow->addWidget(_saveButton);
	buttonRow->addWidget(_deleteButton);
	buttonRow->addStretch();
	layout->addLayout(buttonRow);

	connect(_list, &QListWidget::itemClicked, this, &SceneStatesPanel::onItemClicked);
	connect(_list, &QListWidget::itemSelectionChanged, this, &SceneStatesPanel::onSelectionChanged);
	connect(_saveButton, &QPushButton::clicked, this, &SceneStatesPanel::onSaveButtonClicked);
	connect(_deleteButton, &QPushButton::clicked, this, &SceneStatesPanel::onDeleteButtonClicked);
}

void SceneStatesPanel::setSceneGraph(SceneGraph* sg)
{
	_sceneGraph = sg;
}

void SceneStatesPanel::refresh()
{
	_list->clear();

	if (_sceneGraph)
	{
		for (const SceneState& state : _sceneGraph->sceneStates())
		{
			auto* item = new QListWidgetItem(state.name, _list);
			item->setData(Qt::UserRole, state.id.toString());
		}
	}

	onSelectionChanged();
}

void SceneStatesPanel::onItemClicked(QListWidgetItem* item)
{
	if (!item)
		return;
	const QUuid id(item->data(Qt::UserRole).toString());
	if (!id.isNull())
		emit sceneStateRecallRequested(id);
}

void SceneStatesPanel::onSaveButtonClicked()
{
	bool ok = false;
	const QString name = QInputDialog::getText(this, tr("Save Scene State"),
		tr("Name for this state:"), QLineEdit::Normal, QString(), &ok);
	if (!ok || name.trimmed().isEmpty())
		return;
	emit sceneStateSaveRequested(name.trimmed());
}

void SceneStatesPanel::onDeleteButtonClicked()
{
	QListWidgetItem* item = _list->currentItem();
	if (!item)
		return;
	const QUuid id(item->data(Qt::UserRole).toString());
	if (!id.isNull())
		emit sceneStateDeleteRequested(id);
}

void SceneStatesPanel::onSelectionChanged()
{
	_deleteButton->setEnabled(_list->currentItem() != nullptr);
}
