#include "AnnotationDialog.h"

#include "ModelViewer.h"
#include "SceneGraph.h"
#include "ViewportWidget.h"

#include <QCloseEvent>
#include <QFocusEvent>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace
{
    // Same widget-hierarchy-walking rationale as MeasurementDialog's own
    // findMdiArea() (and RtRenderDialog's before it).
    QMdiArea* findMdiArea(QWidget* widget)
    {
        for (QWidget* w = widget; w; w = w->parentWidget())
        {
            if (auto* area = qobject_cast<QMdiArea*>(w))
                return area;
        }
        return nullptr;
    }

    // Line-for-line mirror of MeasurementDialog.cpp's own MeasurementResultsList -
    // see that class's doc comment for the full "why a plain click on the
    // sole selected row must explicitly deselect" and "why Space needs
    // batching, not a per-row loop" reasoning. Kept as a separate copy
    // rather than a shared base class, matching this codebase's established
    // per-dialog-local-class convention (no shared base exists between the
    // two today either).
    class AnnotationResultsList : public QListWidget
    {
    public:
        explicit AnnotationResultsList(AnnotationDialog* dialog) : QListWidget(dialog), _dialog(dialog) {}

    protected:
        void mousePressEvent(QMouseEvent* event) override
        {
            const QModelIndex index = indexAt(event->pos());
            if (event->button() == Qt::LeftButton && index.isValid() && event->modifiers() == Qt::NoModifier
                && selectionModel() && selectionModel()->isSelected(index)
                && selectionModel()->selectedIndexes().size() == 1)
            {
                clearSelection();
                setCurrentIndex(QModelIndex());
                return;
            }
            QListWidget::mousePressEvent(event);
        }

        void keyPressEvent(QKeyEvent* event) override
        {
            if (event->key() == Qt::Key_Space && selectionModel() && selectionModel()->selectedIndexes().size() > 1)
            {
                QListWidgetItem* current = currentItem();
                if (current && (current->flags() & Qt::ItemIsUserCheckable) && _dialog)
                {
                    const Qt::CheckState newState = (current->checkState() == Qt::Checked) ? Qt::Unchecked : Qt::Checked;
                    _dialog->toggleCheckStatesForSelection(newState);
                    event->accept();
                    return;
                }
            }
            QListWidget::keyPressEvent(event);
        }

    private:
        AnnotationDialog* _dialog;
    };

    // QPlainTextEdit has no built-in "editingFinished"-style signal (unlike
    // QLineEdit) - overriding focusOutEvent is the only way to detect
    // "the user is done editing this note for now" without committing on
    // every keystroke (see AnnotationDialog::commitTextEdit()'s doc comment).
    class AnnotationTextEdit : public QPlainTextEdit
    {
    public:
        explicit AnnotationTextEdit(AnnotationDialog* dialog) : QPlainTextEdit(), _dialog(dialog) {}

    protected:
        void focusOutEvent(QFocusEvent* event) override
        {
            QPlainTextEdit::focusOutEvent(event);
            if (_dialog)
                _dialog->commitTextEdit();
        }

    private:
        AnnotationDialog* _dialog;
    };
}

AnnotationDialog::AnnotationDialog(ModelViewer* modelViewer, QWidget* parent)
    : QDialog(parent)
    , _modelViewer(modelViewer)
{
    setWindowTitle(tr("Annotate"));
    setAttribute(Qt::WA_DeleteOnClose);

    _placeButton = new QPushButton(tr("Place Note"), this);
    _placeButton->setCheckable(true);

    _statusLabel = new QLabel(tr("Click \"Place Note\", then click a point on the model"), this);
    _statusLabel->setWordWrap(true);

    _resultsList = new AnnotationResultsList(this);
    // Ctrl/Shift multi-select, so several notes can be reviewed or deleted
    // together (see ModelViewer::deleteSelectedAnnotations()'s undo-macro
    // batching) - same reasoning as MeasurementDialog's results list.
    _resultsList->setSelectionMode(QAbstractItemView::ExtendedSelection);

    _textEdit = new AnnotationTextEdit(this);
    _textEdit->setEnabled(false);
    _textEdit->setPlaceholderText(tr("Select a single note to edit its text"));
    // No maximum height - grows with the dialog, same as _resultsList
    // (see the stretch factors below), instead of staying pinned to a
    // fixed size while only the list above it gets the extra space.
    _textEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    _deleteButton = new QPushButton(tr("Delete"), this);
    _deleteButton->setEnabled(false);

    QHBoxLayout* buttonRow = new QHBoxLayout();
    buttonRow->addStretch();
    buttonRow->addWidget(_deleteButton);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(_placeButton);
    layout->addWidget(_statusLabel);
    layout->addWidget(new QLabel(tr("Notes:"), this));
    layout->addWidget(_resultsList);
    layout->addWidget(new QLabel(tr("Text:"), this));
    layout->addWidget(_textEdit);
    layout->addLayout(buttonRow);
    // Both grow when the dialog is resized, not just the list - equal
    // stretch factors split the extra space evenly between them.
    layout->setStretchFactor(_resultsList, 1);
    layout->setStretchFactor(_textEdit, 1);
    resize(320, 440);

    connect(_placeButton, &QPushButton::toggled, this, &AnnotationDialog::onPlaceButtonToggled);
    connect(_resultsList, &QListWidget::itemSelectionChanged, this, &AnnotationDialog::onResultsSelectionChanged);
    connect(_resultsList, &QListWidget::itemChanged, this, &AnnotationDialog::onResultItemChanged);
    connect(_deleteButton, &QPushButton::clicked, this, &AnnotationDialog::onDeleteClicked);

    ViewportWidget* viewport = _modelViewer->getViewportWidget();
    connect(viewport, &ViewportWidget::annotationToolArmedChanged, this, &AnnotationDialog::onAnnotationToolArmedChangedExternally);
    connect(viewport, &ViewportWidget::annotationSelectionChanged, this, &AnnotationDialog::onViewportSelectionChanged);
    if (SceneGraph* sceneGraph = _modelViewer->sceneGraph())
        connect(sceneGraph, &SceneGraph::annotationsChanged, this, &AnnotationDialog::onAnnotationsChanged);

    refreshResultsList();

    // Unlike MeasurementDialog (which auto-arms Point on open, since an
    // unarmed Measure dialog is otherwise useless), Annotate's unarmed state
    // is a real, useful one - reviewing/editing existing notes - so the
    // dialog opens without arming placement. The user clicks "Place Note"
    // explicitly when they want to add one.

    if (QMdiArea* mdiArea = findMdiArea(_modelViewer))
        connect(mdiArea, &QMdiArea::subWindowActivated, this, &AnnotationDialog::onActiveSubWindowChanged);

    loadSettings();  // restores window geometry, if any was saved - after resize(320, 440) above so it can override that default
}

AnnotationDialog::~AnnotationDialog()
{
}

void AnnotationDialog::closeEvent(QCloseEvent* event)
{
    // Disarm placement and commit any in-progress edit - closing the dialog
    // with no way to see the pick prompt or the details pane shouldn't leave
    // clicks silently still placing notes, or an unsaved text edit stranded.
    if (ViewportWidget* viewport = _modelViewer->getViewportWidget())
        viewport->setAnnotationToolArmed(false);
    commitTextEdit();
    saveSettings();
    QDialog::closeEvent(event);
}

void AnnotationDialog::loadSettings()
{
    QSettings settings;
    const QByteArray geometry = settings.value("annotation/geometry", QByteArray()).toByteArray();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);
}

void AnnotationDialog::saveSettings()
{
    QSettings settings;
    settings.setValue("annotation/geometry", saveGeometry());
}

void AnnotationDialog::onPlaceButtonToggled(bool checked)
{
    if (ViewportWidget* viewport = _modelViewer->getViewportWidget())
        viewport->setAnnotationToolArmed(checked);
}

void AnnotationDialog::onAnnotationToolArmedChangedExternally(bool armed)
{
    setPlaceButtonCheckedSilently(armed);
    _statusLabel->setText(armed ? tr("Click a point on the model to place a note")
                                 : tr("Click \"Place Note\", then click a point on the model"));
}

void AnnotationDialog::setPlaceButtonCheckedSilently(bool armed)
{
    const QSignalBlocker blocker(_placeButton);
    _placeButton->setChecked(armed);
}

void AnnotationDialog::onAnnotationsChanged()
{
    if (_batchingVisibilityChanges)
        return;  // toggleCheckStatesForSelection() will rebuild once, at the end of its own batch
    refreshResultsList();
}

void AnnotationDialog::toggleCheckStatesForSelection(Qt::CheckState newState)
{
    SceneGraph* sceneGraph = _modelViewer->sceneGraph();
    ViewportWidget* viewport = _modelViewer->getViewportWidget();
    if (!sceneGraph || !viewport)
        return;

    _batchingVisibilityChanges = true;
    for (QListWidgetItem* item : _resultsList->selectedItems())
    {
        if (!(item->flags() & Qt::ItemIsUserCheckable))
            continue;
        const QUuid id = item->data(Qt::UserRole).toUuid();
        sceneGraph->setAnnotationVisible(id, newState == Qt::Checked);
    }
    _batchingVisibilityChanges = false;

    refreshResultsList();
    viewport->update();
}

void AnnotationDialog::refreshResultsList()
{
    SceneGraph* sceneGraph = _modelViewer->sceneGraph();
    ViewportWidget* viewport = _modelViewer->getViewportWidget();
    if (!sceneGraph || !viewport)
        return;

    const QSet<QUuid> selectedIds = viewport->selectedAnnotationIds();

    _updatingSelectionFromViewport = true;
    _resultsList->clear();
    QListWidgetItem* firstSelectedItem = nullptr;
    for (const Annotation& a : sceneGraph->annotations())
    {
        // Row label = first line only, truncated - a note can be several
        // lines long (see AnnotationData.h), but the list is a scannable
        // index, not the place to read the whole thing (that's the details
        // pane below).
        QString firstLine = a.text.section(QChar('\n'), 0, 0);
        constexpr int kMaxRowLabelChars = 60;
        if (firstLine.size() > kMaxRowLabelChars)
            firstLine = firstLine.left(kMaxRowLabelChars) + QStringLiteral("...");
        if (firstLine.isEmpty())
            firstLine = tr("(empty note)");

        QListWidgetItem* item = new QListWidgetItem(firstLine, _resultsList);
        item->setData(Qt::UserRole, a.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(a.visible ? Qt::Checked : Qt::Unchecked);
        if (selectedIds.contains(a.id))
        {
            item->setSelected(true);
            if (!firstSelectedItem)
                firstSelectedItem = item;
        }
    }
    // See MeasurementDialog::refreshResultsList()'s doc comment for why this
    // explicit setCurrentItem() is needed after clear() - same reasoning,
    // same fix, applies identically here.
    if (firstSelectedItem)
        _resultsList->setCurrentItem(firstSelectedItem, QItemSelectionModel::NoUpdate);
    _updatingSelectionFromViewport = false;

    _deleteButton->setEnabled(!selectedIds.isEmpty());
    syncTextEditFromSelection();
}

void AnnotationDialog::syncTextEditFromSelection()
{
    ViewportWidget* viewport = _modelViewer->getViewportWidget();
    SceneGraph* sceneGraph = _modelViewer->sceneGraph();
    if (!viewport || !sceneGraph)
        return;

    const QSet<QUuid> selected = viewport->selectedAnnotationIds();
    if (selected.size() == 1)
    {
        const QUuid id = *selected.constBegin();
        const int index = sceneGraph->annotationIndexById(id);
        if (index >= 0)
        {
            const QString text = sceneGraph->annotations().at(index).text;
            _editingAnnotationId = id;
            _textEditBaseline = text;
            const QSignalBlocker blocker(_textEdit);
            _textEdit->setPlainText(text);
            _textEdit->setEnabled(true);
            return;
        }
    }

    _editingAnnotationId = QUuid();
    _textEditBaseline.clear();
    const QSignalBlocker blocker(_textEdit);
    _textEdit->clear();
    _textEdit->setEnabled(false);
}

void AnnotationDialog::commitTextEdit()
{
    if (_editingAnnotationId.isNull() || !_modelViewer)
        return;

    const QString newText = _textEdit->toPlainText();
    if (newText == _textEditBaseline)
        return;  // focus was lost without an actual edit - don't push a no-op undo step

    _modelViewer->setAnnotationText(_editingAnnotationId, newText);
    _textEditBaseline = newText;
}

void AnnotationDialog::onResultsSelectionChanged()
{
    if (_updatingSelectionFromViewport)
        return;

    ViewportWidget* viewport = _modelViewer->getViewportWidget();
    if (!viewport)
        return;

    QSet<QUuid> ids;
    for (QListWidgetItem* item : _resultsList->selectedItems())
        ids.insert(item->data(Qt::UserRole).toUuid());

    _updatingSelectionFromViewport = true;
    viewport->setSelectedAnnotationIds(ids);
    _updatingSelectionFromViewport = false;

    _deleteButton->setEnabled(!ids.isEmpty());
    syncTextEditFromSelection();
}

void AnnotationDialog::onViewportSelectionChanged(const QSet<QUuid>& ids)
{
    if (_updatingSelectionFromViewport)
        return;

    _updatingSelectionFromViewport = true;
    _resultsList->clearSelection();
    QListWidgetItem* firstMatch = nullptr;
    for (int i = 0; i < _resultsList->count(); ++i)
    {
        QListWidgetItem* item = _resultsList->item(i);
        if (ids.contains(item->data(Qt::UserRole).toUuid()))
        {
            item->setSelected(true);
            if (!firstMatch)
                firstMatch = item;
        }
    }
    if (firstMatch)
        _resultsList->scrollToItem(firstMatch);
    _updatingSelectionFromViewport = false;

    _deleteButton->setEnabled(!ids.isEmpty());
    syncTextEditFromSelection();
}

void AnnotationDialog::onResultItemChanged(QListWidgetItem* item)
{
    if (_updatingSelectionFromViewport || !item)
        return;

    SceneGraph* sceneGraph = _modelViewer->sceneGraph();
    ViewportWidget* viewport = _modelViewer->getViewportWidget();
    if (!sceneGraph)
        return;

    const QUuid id = item->data(Qt::UserRole).toUuid();
    sceneGraph->setAnnotationVisible(id, item->checkState() == Qt::Checked);

    // Same reasoning as MeasurementDialog::onResultItemChanged() - visibility
    // has no other path that happens to trigger a viewport repaint, so it's
    // requested explicitly here.
    if (viewport)
        viewport->update();
}

void AnnotationDialog::onDeleteClicked()
{
    if (_modelViewer)
        _modelViewer->deleteSelectedAnnotations();
}

void AnnotationDialog::onActiveSubWindowChanged(QMdiSubWindow* activeSubWindow)
{
    const bool isOwnDocumentActive = _modelViewer
        && activeSubWindow
        && activeSubWindow->widget() == static_cast<QWidget*>(_modelViewer);
    setVisible(isOwnDocumentActive);
}
