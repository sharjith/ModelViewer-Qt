#pragma once

#include <QDialog>
#include <QSet>
#include <QString>
#include <QUuid>

class ModelViewer;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QPushButton;
class QSplitter;
class QCloseEvent;
class QMdiSubWindow;

// ---------------------------------------------------------------------------
// AnnotationDialog
//
// Non-modal "Annotate" dialog (Tools -> Annotate...) - places free-text
// notes with a leader line, anchored to a point on a mesh's surface. Mirrors
// MeasurementDialog's overall shape (non-modal, per-document, findChild-
// reuse, WA_DeleteOnClose, QSettings geometry persistence) but with a
// smaller surface, since there's only one "tool" (place a note) rather than
// Measure's 15-tool combo:
//   - A single checkable "Place Note" button instead of a tool combo.
//   - A details pane (QPlainTextEdit) below the results list, in a
//     QSplitter with it (drag the handle to resize either) and bound to
//     the currently-selected row, committing via ModelViewer::
//     setAnnotationText() on focus-out (see the nested AnnotationTextEdit
//     class in the .cpp).
//
// Owns no annotation state itself - every control is a thin read/write onto
// the target ModelViewer's ViewportWidget (setAnnotationToolArmed()/
// annotationToolArmed()/selectedAnnotationIds()/setAnnotationText()) and
// SceneGraph (annotations()/annotationsChanged), same "thin front-end"
// relationship MeasurementDialog has with Measurement.
// ---------------------------------------------------------------------------
class AnnotationDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AnnotationDialog(ModelViewer* modelViewer, QWidget* parent = nullptr);
    ~AnnotationDialog();

    // Same batching shape as MeasurementDialog::toggleCheckStatesForSelection() -
    // sets every checkable, currently-selected row in _resultsList to
    // newState in one batch, called from the nested results-list class's
    // keyPressEvent() (Space on a multi-selection). See that method's doc
    // comment in MeasurementDialog.cpp for the full "why not one setCheckState()
    // call per row" reasoning - identical here.
    void toggleCheckStatesForSelection(Qt::CheckState newState);

    // Commits the details pane's current text to the annotation being
    // edited, if it actually changed since syncTextEditFromSelection() last
    // loaded it - called from the nested AnnotationTextEdit's focusOutEvent
    // override (see the .cpp), not per-keystroke. Public for the same
    // reason toggleCheckStatesForSelection() above is - a nested helper
    // class in the .cpp (not a friend) needs to call it, not just this
    // dialog's own slots.
    void commitTextEdit();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onPlaceButtonToggled(bool checked);
    void onResultsSelectionChanged();
    void onDeleteClicked();
    void onAnnotationToolArmedChangedExternally(bool armed);
    void onAnnotationsChanged();
    // Viewport -> dialog direction of selection sync (an annotation was
    // clicked/drag-selected directly in the 3D view) - distinct from
    // onResultsSelectionChanged() (dialog -> viewport), same split
    // MeasurementDialog makes and for the same reason.
    void onViewportSelectionChanged(const QSet<QUuid>& ids);
    // Fires for the checkbox (show/hide) and, harmlessly, any other
    // per-item change QListWidget might report - only the check state is
    // acted on, same convention as MeasurementDialog::onResultItemChanged().
    void onResultItemChanged(QListWidgetItem* item);
    void onActiveSubWindowChanged(QMdiSubWindow* activeSubWindow);

private:
    void refreshResultsList();
    // Selects the "Place Note" button's checked state without re-triggering
    // onPlaceButtonToggled()'s own setAnnotationToolArmed() call - needed
    // when the sync is going the OTHER way (viewport -> dialog, e.g. Escape
    // cancelling the tool, or the Measure tool disarming Annotate via their
    // mutual exclusivity - see AnnotationController.h). Mirrors
    // MeasurementDialog::setComboToolSilently()'s QSignalBlocker idiom.
    void setPlaceButtonCheckedSilently(bool armed);
    // Loads the single selected annotation's live text into the details
    // pane (and its baseline for change-detection - see commitTextEdit()),
    // or clears/disables the pane when zero or more than one row is
    // selected (editing only makes sense for exactly one at a time).
    void syncTextEditFromSelection();

    void loadSettings();
    void saveSettings();

    ModelViewer*    _modelViewer; // not owned - dialog is a child of the ModelViewer's window
    QPushButton*    _placeButton;
    QLabel*         _statusLabel;
    QListWidget*    _resultsList;
    QPlainTextEdit* _textEdit;
    // Lets the user drag the boundary between the results list and the
    // details pane instead of a fixed 50/50 split - state persisted the
    // same QSettings way as window geometry (see loadSettings()/
    // saveSettings()).
    QSplitter*      _splitter;
    QPushButton*    _deleteButton;

    // Which annotation the details pane currently reflects - QUuid() when
    // none/multiple are selected (pane disabled in that case).
    QUuid   _editingAnnotationId;
    // The text as last loaded into _textEdit (by syncTextEditFromSelection()
    // or a successful commitTextEdit()) - commitTextEdit() compares against
    // this to skip a no-op undo push when focus is simply lost without an
    // actual edit.
    QString _textEditBaseline;

    // Same reentrancy-guard role as MeasurementDialog::_updatingSelectionFromViewport.
    bool _updatingSelectionFromViewport = false;
    // Same reentrancy-guard role as MeasurementDialog::_batchingVisibilityChanges.
    bool _batchingVisibilityChanges = false;
};
