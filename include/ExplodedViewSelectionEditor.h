#pragma once

#include <QDialog>
#include <QVector>
#include <QUuid>

#include "ui_ExplodedViewSelectionEditor.h"

class QListWidgetItem;

class ExplodedViewSelectionEditor : public QDialog, private Ui::ExplodedViewSelectionEditor
{
    Q_OBJECT

public:
    struct Entry
    {
        QUuid uuid;
        QString label;
    };

    static constexpr int AddMoreResult = 2;

    explicit ExplodedViewSelectionEditor(QWidget* parent = nullptr);

    void setEntries(const QVector<Entry>& entries);
    QVector<Entry> entries() const;

    // Lets another tool (Mass Properties) reuse this editor with wording that fits it - the defaults talk about an
    // "assembly".
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
