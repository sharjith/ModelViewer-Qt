#include "ExplodedViewSelectionEditor.h"

#include "LanguageManager.h"

#include <QListWidgetItem>

namespace
{
constexpr int kUuidRole = Qt::UserRole;
}

ExplodedViewSelectionEditor::ExplodedViewSelectionEditor(QWidget* parent)
    : QDialog(parent)
{
    setupUi(this);

    // See ExplodedViewPanel's/ClippingPlanesEditor's identical connection -
    // without this, a live language switch in Settings left this dialog
    // showing whatever language was active at construction until the next
    // app restart.
    connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
        retranslateUi(this);
        });

    connect(listWidgetMembers, &QListWidget::currentItemChanged,
            this, &ExplodedViewSelectionEditor::onCurrentItemChanged);
    connect(listWidgetMembers, &QListWidget::itemSelectionChanged,
            this, &ExplodedViewSelectionEditor::updateSelectionUi);
    connect(pushButtonRemove, &QPushButton::clicked,
            this, &ExplodedViewSelectionEditor::onRemoveClicked);
    connect(pushButtonAdd, &QPushButton::clicked,
            this, &ExplodedViewSelectionEditor::onAddClicked);
    connect(pushButtonDone, &QPushButton::clicked,
            this, &ExplodedViewSelectionEditor::onDoneClicked);
    connect(pushButtonCancel, &QPushButton::clicked,
            this, &ExplodedViewSelectionEditor::onCancelClicked);

    pushButtonRemove->setEnabled(false);
}

void ExplodedViewSelectionEditor::setEntries(const QVector<Entry>& entries)
{
    listWidgetMembers->clear();
    for (const Entry& entry : entries)
    {
        auto* item = new QListWidgetItem(entry.label, listWidgetMembers);
        item->setData(kUuidRole, entry.uuid);
    }

    if (listWidgetMembers->count() > 0)
        listWidgetMembers->setCurrentRow(0);
    else
        labelSelected->setText(tr("Selected: None"));

    updateSelectionUi();
}

QVector<ExplodedViewSelectionEditor::Entry> ExplodedViewSelectionEditor::entries() const
{
    QVector<Entry> out;
    out.reserve(listWidgetMembers->count());
    for (int index = 0; index < listWidgetMembers->count(); ++index)
    {
        const QListWidgetItem* item = listWidgetMembers->item(index);
        if (!item)
            continue;

        Entry entry;
        entry.uuid = item->data(kUuidRole).toUuid();
        entry.label = item->text();
        out.append(entry);
    }
    return out;
}

void ExplodedViewSelectionEditor::onCurrentItemChanged(QListWidgetItem* current, QListWidgetItem* previous)
{
    Q_UNUSED(previous);
    if (current)
        emit previewEntryRequested(current->data(kUuidRole).toUuid());
}

void ExplodedViewSelectionEditor::onRemoveClicked()
{
    const QList<QListWidgetItem*> selectedItems = listWidgetMembers->selectedItems();
    if (selectedItems.isEmpty())
        return;

    int fallbackRow = listWidgetMembers->row(selectedItems.constFirst());
    for (QListWidgetItem* item : selectedItems)
        delete listWidgetMembers->takeItem(listWidgetMembers->row(item));

    if (listWidgetMembers->count() > 0)
        listWidgetMembers->setCurrentRow(qBound(0, fallbackRow, listWidgetMembers->count() - 1));
    else
    {
        labelSelected->setText(tr("Selected: None"));
        pushButtonRemove->setEnabled(false);
    }

    updateSelectionUi();
}

void ExplodedViewSelectionEditor::onAddClicked()
{
    done(AddMoreResult);
}

void ExplodedViewSelectionEditor::onDoneClicked()
{
    accept();
}

void ExplodedViewSelectionEditor::onCancelClicked()
{
    reject();
}

void ExplodedViewSelectionEditor::updateSelectionUi()
{
    const QList<QListWidgetItem*> selectedItems = listWidgetMembers->selectedItems();
    if (selectedItems.isEmpty())
    {
        labelSelected->setText(tr("Selected: None"));
        pushButtonRemove->setEnabled(false);
        return;
    }

    if (selectedItems.size() == 1)
    {
        labelSelected->setText(tr("Selected: %1").arg(selectedItems.constFirst()->text()));
    }
    else
    {
        labelSelected->setText(tr("Selected: %1 items").arg(selectedItems.size()));
    }

    pushButtonRemove->setEnabled(true);
}
