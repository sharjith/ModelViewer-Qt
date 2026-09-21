#include "MeshSelectionBox.h"
#include "MeshSelectionEditor.h"
#include "ModelViewer.h"
#include "ViewportWidget.h"
#include "SceneMesh.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QTimer>
#include <utility>

MeshSelectionBox::MeshSelectionBox(ModelViewer* modelViewer, QWidget* parent)
	: MeshSelectionBox(parent)
{
	setModelViewer(modelViewer);
}

MeshSelectionBox::MeshSelectionBox(QWidget* parent)
	: QWidget(parent)
{
	auto* row = new QHBoxLayout(this);
	row->setContentsMargins(0, 0, 0, 0);

	_label = new QLabel(tr("Selection:"), this);
	row->addWidget(_label);

	_field = new QLineEdit(this);
	_field->setReadOnly(true);
	_field->setPlaceholderText(emptyPlaceholder());
	_field->setToolTip(tr("The selected meshes. Right-click to edit or clear."));
	_field->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(_field, &QWidget::customContextMenuRequested, this, &MeshSelectionBox::showContextMenu);
	row->addWidget(_field, 1);

	const auto makeIconButton = [this](const QString& iconPath, const QString& tip, bool checkable) {
		auto* button = new QPushButton(this);
		button->setIcon(QIcon(iconPath));
		button->setFixedSize(28, 28);
		button->setCheckable(checkable);
		button->setToolTip(tip);
		return button;
	};
	_idlePickIcon = QIcon(QStringLiteral(":/icons/res/select.png"));
	_confirmPickIcon = QIcon(QStringLiteral(":/icons/res/checkmark.png"));
	_pickButton = makeIconButton(QStringLiteral(":/icons/res/select.png"), QString(), true);
	connect(_pickButton, &QPushButton::toggled, this, &MeshSelectionBox::onPickToggled);
	row->addWidget(_pickButton);
	_editButton = makeIconButton(QStringLiteral(":/icons/res/edit_selection.png"), tr("Edit Selection..."), false);
	connect(_editButton, &QPushButton::clicked, this, &MeshSelectionBox::editSelection);
	row->addWidget(_editButton);
	_clearButton = makeIconButton(QStringLiteral(":/icons/res/clear.png"), tr("Clear Selection"), false);
	connect(_clearButton, &QPushButton::clicked, this, &MeshSelectionBox::clearSelection);
	row->addWidget(_clearButton);

	updatePickVisual();
	updateDisplay();
}

void MeshSelectionBox::setModelViewer(ModelViewer* modelViewer)
{
	_modelViewer = modelViewer;
	if (_modelViewer && _modelViewer->getViewportWidget())
	{
		connect(_modelViewer->getViewportWidget(), &ViewportWidget::meshAboutToBeDeleted,
			this, &MeshSelectionBox::onMeshAboutToBeDeleted, Qt::UniqueConnection);
	}
}

std::vector<int> MeshSelectionBox::meshIds() const
{
	std::vector<int> ids;
	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	if (!viewport)
		return ids;
	for (const QUuid& uuid : _uuids)
	{
		const int id = viewport->getIndexByUuid(uuid);
		if (id >= 0)
			ids.push_back(id);
	}
	return ids;
}

void MeshSelectionBox::setMeshUuids(const QVector<QUuid>& uuids)
{
	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	QVector<QUuid> cleaned;
	for (const QUuid& uuid : uuids)
	{
		if (uuid.isNull() || cleaned.contains(uuid) || _excluded.contains(uuid))
			continue;
		if (viewport && viewport->getIndexByUuid(uuid) < 0)
			continue; // no longer in the scene
		cleaned.append(uuid);
	}
	if (_single && cleaned.size() > 1)
		cleaned.resize(1);
	_uuids = cleaned;
	updateDisplay();
	emit meshUuidsChanged();
}

void MeshSelectionBox::seedFromViewportSelection()
{
	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	if (!viewport)
		return;
	QVector<QUuid> uuids;
	for (int id : _modelViewer->getSelectedIDs())
	{
		const QUuid uuid = viewport->getUuidByIndex(id);
		if (!uuid.isNull() && !uuids.contains(uuid))
			uuids.append(uuid);
	}
	if (!uuids.isEmpty())
		setMeshUuids(uuids);
}

void MeshSelectionBox::setSingleMeshMode(bool single)
{
	_single = single;
	_editButton->setVisible(!single);
	updatePickVisual();
	_field->setPlaceholderText(emptyPlaceholder());
	if (single && _uuids.size() > 1)
		setMeshUuids(_uuids);
}

void MeshSelectionBox::setExcludedUuids(const QVector<QUuid>& excluded)
{
	_excluded = excluded;
	QVector<QUuid> kept;
	for (const QUuid& uuid : std::as_const(_uuids))
	{
		if (!_excluded.contains(uuid))
			kept.append(uuid);
	}
	if (kept.size() != _uuids.size())
		setMeshUuids(kept);
}

void MeshSelectionBox::setLabelText(const QString& text)
{
	_label->setText(text);
}

QString MeshSelectionBox::emptyPlaceholder() const
{
	return _single ? tr("Select a mesh...") : tr("Select meshes...");
}

void MeshSelectionBox::addViewportSelection()
{
	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	if (!viewport)
		return;
	QVector<QUuid> merged = _uuids;
	for (int id : _modelViewer->getSelectedIDs())
	{
		const QUuid uuid = viewport->getUuidByIndex(id);
		if (uuid.isNull() || _excluded.contains(uuid) || merged.contains(uuid))
			continue;
		if (_single)
			merged.clear(); // the picked mesh replaces the current one
		merged.append(uuid);
		if (_single)
			break;
	}
	if (merged != _uuids)
		setMeshUuids(merged);
}

void MeshSelectionBox::setFieldToolTip(const QString& text)
{
	_field->setToolTip(text);
}

void MeshSelectionBox::setEditorTexts(const QString& intro, const QString& members)
{
	_editorIntro = intro;
	_editorMembers = members;
}

bool MeshSelectionBox::isPicking() const
{
	return _pickButton && _pickButton->isChecked();
}

QString MeshSelectionBox::describe() const
{
	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	if (_uuids.isEmpty() || !viewport)
		return QString();
	if (_uuids.size() == 1)
	{
		if (SceneMesh* mesh = viewport->getMeshByUuid(_uuids.first()))
			return mesh->getName();
	}
	return tr("%1 meshes").arg(_uuids.size());
}

void MeshSelectionBox::updateDisplay()
{
	_field->setText(describe());
	_editButton->setEnabled(!_uuids.isEmpty());
	_clearButton->setEnabled(!_uuids.isEmpty());
}

void MeshSelectionBox::updatePickVisual()
{
	const bool picking = _pickButton->isChecked();
	_pickButton->setIcon(picking ? _confirmPickIcon : _idlePickIcon);
	_pickButton->setToolTip(picking
		? tr("Adjust scene or tree selection, then click again to confirm")
		: (_single ? tr("Pick a mesh from the scene or tree, then click again to confirm")
		           : tr("Add meshes from the scene or tree, then click again to confirm")));
}

void MeshSelectionBox::stopPicking()
{
	if (!_pickButton->isChecked())
		return;
	{
		QSignalBlocker blocker(_pickButton);
		_pickButton->setChecked(false);
	}
	updatePickVisual();
	_field->setPlaceholderText(emptyPlaceholder());
}

void MeshSelectionBox::onPickToggled(bool checked)
{
	updatePickVisual();
	if (checked)
	{
		// Picking happens in the viewport/tree, which stay usable because the tool dialogs are non-modal.
		_field->setPlaceholderText(_single ? tr("Pick a mesh, then click again to confirm...")
		                                   : tr("Add meshes, then click again to confirm..."));
		return;
	}

	_field->setPlaceholderText(emptyPlaceholder());
	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	if (!viewport)
		return;
	QVector<QUuid> picked;
	for (int id : _modelViewer->getSelectedIDs())
	{
		const QUuid uuid = viewport->getUuidByIndex(id);
		if (!uuid.isNull() && !picked.contains(uuid) && !_excluded.contains(uuid))
			picked.append(uuid);
	}
	// Single-mesh mode: the picked mesh replaces the current one (if nothing usable was picked, it stays).
	QVector<QUuid> merged = _single ? (picked.isEmpty() ? _uuids : QVector<QUuid>{ picked.first() }) : _uuids;
	if (!_single)
	{
		for (const QUuid& uuid : std::as_const(picked))
		{
			if (!merged.contains(uuid))
				merged.append(uuid);
		}
	}
	// The picked meshes now live in the list - clear the viewport selection so the next pick starts fresh.
	_modelViewer->setSelectionWithoutUndo(QSet<int>());
	setMeshUuids(merged);
}

void MeshSelectionBox::editSelection()
{
	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	if (!viewport || _uuids.isEmpty())
		return;

	MeshSelectionEditor editor(this);
	if (!_editorIntro.isEmpty())
		editor.setIntroText(_editorIntro);
	if (!_editorMembers.isEmpty())
		editor.setMembersText(_editorMembers);
	QVector<MeshSelectionEditor::Entry> entries;
	for (const QUuid& uuid : std::as_const(_uuids))
	{
		if (SceneMesh* mesh = viewport->getMeshByUuid(uuid))
			entries.append({ uuid, mesh->getName() });
	}
	editor.setEntries(entries);

	// Highlighting the entry under the cursor shows where that mesh is; the previous selection is put back afterwards.
	QSet<int> previousSelection;
	for (int id : _modelViewer->getSelectedIDs())
		previousSelection.insert(id);
	connect(&editor, &MeshSelectionEditor::previewEntryRequested, this, [this](const QUuid& uuid) {
		_modelViewer->setSelectionWithoutUndo(QSet<QUuid>{ uuid });
	});

	const int result = editor.exec();
	_modelViewer->setSelectionWithoutUndo(previousSelection);
	if (result != QDialog::Accepted && result != MeshSelectionEditor::AddMoreResult)
		return;

	QVector<QUuid> updated;
	for (const MeshSelectionEditor::Entry& entry : editor.entries())
		updated.append(entry.uuid);
	setMeshUuids(updated);
	if (result == MeshSelectionEditor::AddMoreResult)
		_pickButton->setChecked(true);
}

void MeshSelectionBox::clearSelection()
{
	stopPicking();
	setMeshUuids(QVector<QUuid>());
}

void MeshSelectionBox::showContextMenu(const QPoint& pos)
{
	if (_uuids.isEmpty())
		return;
	QMenu menu(this);
	if (!_single)
	{
		connect(menu.addAction(QIcon(QStringLiteral(":/icons/res/edit_selection.png")), tr("Edit Selection...")),
			&QAction::triggered, this, &MeshSelectionBox::editSelection);
		menu.addSeparator();
	}
	connect(menu.addAction(QIcon(QStringLiteral(":/icons/res/clear.png")), tr("Clear Selection")),
		&QAction::triggered, this, &MeshSelectionBox::clearSelection);
	menu.exec(_field->mapToGlobal(pos));
}

void MeshSelectionBox::onMeshAboutToBeDeleted(SceneMesh* mesh)
{
	const QUuid uuid = mesh ? mesh->uuid() : QUuid();
	if (uuid.isNull() || !_uuids.contains(uuid))
		return;
	// The mesh still exists while this signal is delivered, so the list is updated once the deletion has happened.
	QTimer::singleShot(0, this, [this, uuid]() {
		if (_uuids.removeAll(uuid) > 0)
		{
			updateDisplay();
			emit meshUuidsChanged();
		}
	});
}
