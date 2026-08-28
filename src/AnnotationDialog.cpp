#include "AnnotationDialog.h"
#include "ui_AnnotationDialog.h"

#include "ModelViewer.h"
#include "SceneGraph.h"
#include "ViewportWidget.h"

#include <QCloseEvent>
#include <QFocusEvent>
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
#include <QSplitter>

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
    , ui(std::make_unique<Ui::AnnotationDialog>())
{
    ui->setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);

    // resultsList/textEdit are plain placeholders in the .ui (Designer
    // controls their position/size there) - swapped here for the real
    // subclasses, which Designer can't construct directly since they're
    // private, header-less classes local to this file. See
    // MeasurementDialog's identical swap for the same reasoning.
    AnnotationResultsList* resultsList = new AnnotationResultsList(this);
    resultsList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->notesLayout->replaceWidget(ui->resultsList, resultsList);
    delete ui->resultsList;
    ui->resultsList = resultsList;

    AnnotationTextEdit* textEdit = new AnnotationTextEdit(this);
    textEdit->setEnabled(ui->textEdit->isEnabled());
    textEdit->setPlaceholderText(ui->textEdit->placeholderText());
    ui->textPaneLayout->replaceWidget(ui->textEdit, textEdit);
    delete ui->textEdit;
    ui->textEdit = textEdit;

    // Equal-weight split by default - not expressible as a plain .ui
    // property, set here same as the original hand-built constructor did.
    ui->splitter->setStretchFactor(0, 1);
    ui->splitter->setStretchFactor(1, 1);

    connect(ui->placeButton, &QPushButton::toggled, this, &AnnotationDialog::onPlaceButtonToggled);
    connect(ui->resultsList, &QListWidget::itemSelectionChanged, this, &AnnotationDialog::onResultsSelectionChanged);
    connect(ui->resultsList, &QListWidget::itemChanged, this, &AnnotationDialog::onResultItemChanged);
    connect(ui->deleteButton, &QPushButton::clicked, this, &AnnotationDialog::onDeleteClicked);

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

    loadSettings();  // restores window geometry, if any was saved - after the .ui's own geometry so it can override that default
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

    const QByteArray splitterState = settings.value("annotation/splitterState", QByteArray()).toByteArray();
    if (!splitterState.isEmpty())
        ui->splitter->restoreState(splitterState);
}

void AnnotationDialog::saveSettings()
{
    QSettings settings;
    settings.setValue("annotation/geometry", saveGeometry());
    settings.setValue("annotation/splitterState", ui->splitter->saveState());
}

void AnnotationDialog::onPlaceButtonToggled(bool checked)
{
    if (ViewportWidget* viewport = _modelViewer->getViewportWidget())
        viewport->setAnnotationToolArmed(checked);
}

void AnnotationDialog::onAnnotationToolArmedChangedExternally(bool armed)
{
    setPlaceButtonCheckedSilently(armed);
    ui->statusLabel->setText(armed ? tr("Click a point on the model to place a note")
                                    : tr("Click \"Place Note\", then click a point on the model"));
}

void AnnotationDialog::setPlaceButtonCheckedSilently(bool armed)
{
    const QSignalBlocker blocker(ui->placeButton);
    ui->placeButton->setChecked(armed);
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
    for (QListWidgetItem* item : ui->resultsList->selectedItems())
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
    ui->resultsList->clear();
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

        QListWidgetItem* item = new QListWidgetItem(firstLine, ui->resultsList);
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
        ui->resultsList->setCurrentItem(firstSelectedItem, QItemSelectionModel::NoUpdate);
    _updatingSelectionFromViewport = false;

    ui->deleteButton->setEnabled(!selectedIds.isEmpty());
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
            const QSignalBlocker blocker(ui->textEdit);
            ui->textEdit->setPlainText(text);
            ui->textEdit->setEnabled(true);
            return;
        }
    }

    _editingAnnotationId = QUuid();
    _textEditBaseline.clear();
    const QSignalBlocker blocker(ui->textEdit);
    ui->textEdit->clear();
    ui->textEdit->setEnabled(false);
}

void AnnotationDialog::commitTextEdit()
{
    if (_editingAnnotationId.isNull() || !_modelViewer)
        return;

    const QString newText = ui->textEdit->toPlainText();
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
    for (QListWidgetItem* item : ui->resultsList->selectedItems())
        ids.insert(item->data(Qt::UserRole).toUuid());

    _updatingSelectionFromViewport = true;
    viewport->setSelectedAnnotationIds(ids);
    _updatingSelectionFromViewport = false;

    ui->deleteButton->setEnabled(!ids.isEmpty());
    syncTextEditFromSelection();
}

void AnnotationDialog::onViewportSelectionChanged(const QSet<QUuid>& ids)
{
    if (_updatingSelectionFromViewport)
        return;

    _updatingSelectionFromViewport = true;
    ui->resultsList->clearSelection();
    QListWidgetItem* firstMatch = nullptr;
    for (int i = 0; i < ui->resultsList->count(); ++i)
    {
        QListWidgetItem* item = ui->resultsList->item(i);
        if (ids.contains(item->data(Qt::UserRole).toUuid()))
        {
            item->setSelected(true);
            if (!firstMatch)
                firstMatch = item;
        }
    }
    if (firstMatch)
        ui->resultsList->scrollToItem(firstMatch);
    _updatingSelectionFromViewport = false;

    ui->deleteButton->setEnabled(!ids.isEmpty());
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

    // Same reasoning as MeasurementDialog::onResultItemChanged() -
    // refreshMeasurementAnnotationBounds() both repaints AND recomputes
    // bounds/conditionally re-fits, so this checkbox toggle gets the same
    // "Auto Fit View On Hide/Show" treatment the viewport context menu's
    // Hide/Show already gets.
    if (viewport)
        viewport->refreshMeasurementAnnotationBounds();
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
