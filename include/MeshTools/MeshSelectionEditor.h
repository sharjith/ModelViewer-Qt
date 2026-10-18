#pragma once

#include <QDialog>
#include <QVector>
#include <QUuid>

#include "ui_MeshSelectionEditor.h"

class QListWidgetItem;

// A small list editor for a set of meshes: shows the meshes (by name), lets the user remove some, and offers
// "Add..." to hand control back to the caller so it can gather more meshes. Used by the Exploded View panel (an
// assembly's members) and the Mass Properties dialog (the meshes in the report). The caller owns the meaning of
// the list: this class only edits (uuid, label) entries and reports Accepted / Rejected / AddMoreResult.
class MeshSelectionEditor : public QDialog, private Ui::MeshSelectionEditor
{
    Q_OBJECT

public:
    struct Entry
    {
        QUuid uuid;
        QString label;
    };

    static constexpr int AddMoreResult = 2;

    explicit MeshSelectionEditor(QWidget* parent = nullptr);

    void setEntries(const QVector<Entry>& entries);
    QVector<Entry> entries() const;

    // The wording is neutral by default; a caller whose list has a more specific meaning (Exploded View: an
    // assembly's members) sets its own. Set after construction - a live language switch while the editor is open
    // reverts to the neutral defaults.
    void setIntroText(const QString& text);
    void setMembersText(const QString& text);

signals:
    void previewEntryRequested(const QUuid& uuid);

private slots:
    void onCurrentItemChanged(QListWidgetItem* current, QListWidgetItem* previous);
    void onRemoveClicked();
    void onAddClicked();
    void onDoneClicked();
    void onCancelClicked();
    void updateSelectionUi();
};
