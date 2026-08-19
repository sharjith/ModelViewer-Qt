#include "MaterialVariantsPanel.h"
#include "SceneGraph.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// Icon helpers
// ---------------------------------------------------------------------------

static QIcon makeCircleIcon(bool filled, const QColor& color)
{
    constexpr int S = 16;
    QPixmap pm(S, S);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 1.25));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(2.0, 2.0, S - 4.0, S - 4.0));
    if (filled)
    {
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QRectF(5.1, 5.1, S - 10.2, S - 10.2));
    }
    p.end();

    return QIcon(pm);
}

// ---------------------------------------------------------------------------
// MaterialVariantsPanel
// ---------------------------------------------------------------------------

MaterialVariantsPanel::MaterialVariantsPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    _tree = new QTreeWidget(this);
    _tree->setHeaderHidden(true);
    _tree->setColumnCount(1);
    _tree->setRootIsDecorated(true);
    _tree->setIndentation(16);
    _tree->setAlternatingRowColors(true);
    _tree->setSelectionMode(QAbstractItemView::NoSelection);
    _tree->setFocusPolicy(Qt::NoFocus);
    _tree->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(_tree, 1);

    _captureButton = new QPushButton(tr("Add Variant..."), this);
    _captureButton->setToolTip(tr("Capture the current file's live material state as a new variant"));
    _setDefaultButton = new QPushButton(tr("Set as Default"), this);
    _setDefaultButton->setToolTip(tr("Make the active variant's material the file's fallback/default"));
    _deleteButton = new QPushButton(tr("Delete"), this);
    _deleteButton->setToolTip(tr("Delete the active variant"));

    auto* buttonRow = new QHBoxLayout();
    buttonRow->setContentsMargins(8, 0, 8, 8);
    buttonRow->setSpacing(8);
    buttonRow->addWidget(_captureButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(_setDefaultButton);
    buttonRow->addWidget(_deleteButton);
    layout->addLayout(buttonRow);

    connect(_tree, &QTreeWidget::itemClicked,
            this,  &MaterialVariantsPanel::onItemClicked);
    connect(_tree, &QWidget::customContextMenuRequested,
            this, &MaterialVariantsPanel::onTreeContextMenuRequested);
    connect(_captureButton, &QPushButton::clicked, this, &MaterialVariantsPanel::onCaptureButtonClicked);
    connect(_setDefaultButton, &QPushButton::clicked, this, &MaterialVariantsPanel::onSetDefaultButtonClicked);
    connect(_deleteButton, &QPushButton::clicked, this, &MaterialVariantsPanel::onDeleteButtonClicked);

    updateButtonStates();
}

void MaterialVariantsPanel::setSceneGraph(SceneGraph* sg)
{
    _sceneGraph = sg;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void MaterialVariantsPanel::refresh()
{
    _tree->clear();

    if (!_sceneGraph)
    {
        _currentFile.clear();
        updateButtonStates();
        return;
    }

    // Every loaded file gets a row, not just ones that already carry
    // KHR_materials_variants - "Capture Current as Variant..." needs to be
    // reachable on plain STEP/OBJ/etc. files too, so users can build up
    // variants from scratch before exporting to glTF/GLB.
    const QStringList files = _sceneGraph->allSourceFiles();
    if (!files.contains(_currentFile))
        _currentFile = files.isEmpty() ? QString() : files.first();

    for (const QString& sourceFile : files)
    {
        const GltfVariantData vd  = _sceneGraph->variantDataForFile(sourceFile);
        const int             active = _sceneGraph->activeVariantForFile(sourceFile);

        // --- Single-file: one tree item per file ---
        const QString displayName = QFileInfo(sourceFile).fileName();
        QTreeWidgetItem* fileItem = makeFileItem(sourceFile, displayName);
        _tree->addTopLevelItem(fileItem);

        // "Default" entry always first (variantIndex -1)
        fileItem->addChild(makeVariantItem(tr("Default"), -1, active == -1));

        for (int i = 0; i < vd.variantNames.size(); ++i)
            fileItem->addChild(makeVariantItem(vd.variantNames[i], i, active == i));

        fileItem->setExpanded(true);
    }

    updateButtonStates();
}

void MaterialVariantsPanel::setDetachedOverlayMode(bool enabled)
{
    if (_overlayMode == enabled)
        return;

    if (enabled)
    {
        _savedStyleSheet        = _tree->styleSheet();
        _savedPalette           = _tree->palette();
        _savedViewportPalette   = _tree->viewport()->palette();
        _savedAutoFill          = _tree->autoFillBackground();
        _savedViewportAutoFill  = _tree->viewport()->autoFillBackground();

        QPalette p = _savedPalette;
        QColor base      = p.color(QPalette::Base);
        QColor alternate = p.color(QPalette::AlternateBase);
        base.setAlpha(0);
        alternate.setAlpha(0);
        p.setColor(QPalette::Base, base);
        p.setColor(QPalette::AlternateBase, alternate);

        _tree->setPalette(p);
        _tree->viewport()->setPalette(p);
        _tree->setAutoFillBackground(false);
        _tree->viewport()->setAutoFillBackground(false);
        _tree->setAttribute(Qt::WA_NoSystemBackground, true);
        _tree->viewport()->setAttribute(Qt::WA_NoSystemBackground, true);
        _tree->viewport()->setAttribute(Qt::WA_StyledBackground, false);
        setProperty("detachedOverlayMode", true);
        _tree->setProperty("detachedOverlayMode", true);
        _tree->viewport()->setProperty("detachedOverlayMode", true);
        _tree->setStyleSheet(QString());

        setAttribute(Qt::WA_NoSystemBackground, true);
        setAutoFillBackground(false);
    }
    else
    {
        _tree->setStyleSheet(_savedStyleSheet);
        _tree->setPalette(_savedPalette);
        _tree->viewport()->setPalette(_savedViewportPalette);
        _tree->setAutoFillBackground(_savedAutoFill);
        _tree->viewport()->setAutoFillBackground(_savedViewportAutoFill);
        _tree->setAttribute(Qt::WA_NoSystemBackground, false);
        _tree->viewport()->setAttribute(Qt::WA_NoSystemBackground, false);
        _tree->viewport()->setAttribute(Qt::WA_StyledBackground, false);
        setProperty("detachedOverlayMode", false);
        _tree->setProperty("detachedOverlayMode", false);
        _tree->viewport()->setProperty("detachedOverlayMode", false);

        setAttribute(Qt::WA_NoSystemBackground, false);
        setAutoFillBackground(true);
    }

    _overlayMode = enabled;
    refreshDetachedOverlayTheme();
    _tree->viewport()->update();
    _tree->update();
    update();
}

void MaterialVariantsPanel::refreshDetachedOverlayTheme()
{
    if (!_overlayMode || !_tree)
        return;

    const bool lightText = property("overlayViewerLightText").toBool();
    const QColor textColor = lightText ? QColor(255, 255, 255) : QColor(0, 0, 0);
    _detachedOverlayFillColor = lightText ? QColor(255, 255, 255, 65) : QColor(0, 0, 0, 45);

    QPalette treePalette = _tree->palette();
    treePalette.setColor(QPalette::Text, textColor);
    treePalette.setColor(QPalette::WindowText, textColor);
    treePalette.setColor(QPalette::ButtonText, textColor);
    treePalette.setColor(QPalette::HighlightedText, textColor);
    _tree->setPalette(treePalette);

    QPalette viewportPalette = _tree->viewport()->palette();
    viewportPalette.setColor(QPalette::Text, textColor);
    viewportPalette.setColor(QPalette::WindowText, textColor);
    viewportPalette.setColor(QPalette::ButtonText, textColor);
    viewportPalette.setColor(QPalette::HighlightedText, textColor);
    _tree->viewport()->setPalette(viewportPalette);

    if (_sceneGraph)
        refresh();
}

void MaterialVariantsPanel::paintEvent(QPaintEvent* event)
{
    if (_overlayMode)
    {
        QPainter painter(this);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(event->rect(), _detachedOverlayFillColor);
    }

    QWidget::paintEvent(event);
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void MaterialVariantsPanel::onItemClicked(QTreeWidgetItem* item, int /*column*/)
{
    if (!item)
        return;

    const bool isFileItem = item->data(0, IsFileItemRole).toBool();
    if (isFileItem)
    {
        // Clicking the file label doesn't activate anything, but it's still
        // how the user points the bottom buttons at a file with no variants
        // of its own yet.
        _currentFile = item->data(0, SourceFileRole).toString();
        updateButtonStates();
        return;
    }

    // SourceFileRole is stored on the file-level parent, not on variant items.
    QTreeWidgetItem* parentItem = item->parent();
    const QString sourceFile = parentItem
        ? parentItem->data(0, SourceFileRole).toString()
        : QString();
    const int variantIndex = item->data(0, VariantIndexRole).toInt();

    if (sourceFile.isEmpty())
        return;

    _currentFile = sourceFile;

    // Update radio icons for this file's children
    markActiveVariant(sourceFile, variantIndex);
    updateButtonStates();

    emit variantActivated(sourceFile, variantIndex);
}

void MaterialVariantsPanel::onTreeContextMenuRequested(const QPoint& pos)
{
    if (!_tree)
        return;

    QTreeWidgetItem* item = _tree->itemAt(pos);
    if (!item)
        return;

    const bool isFileItem = item->data(0, IsFileItemRole).toBool();
    const QString sourceFile = isFileItem
        ? item->data(0, SourceFileRole).toString()
        : (item->parent() ? item->parent()->data(0, SourceFileRole).toString() : QString());
    const int variantIndex = isFileItem ? -1 : item->data(0, VariantIndexRole).toInt();

    if (sourceFile.isEmpty())
        return;
    if (!isFileItem && variantIndex < 0)
        return;

    QMenu menu(this);
    QAction* captureAction = isFileItem ? menu.addAction(tr("Capture Current as Variant...")) : nullptr;
    QAction* setDefaultAction = !isFileItem ? menu.addAction(tr("Set as Default")) : nullptr;
    menu.addSeparator();
    QAction* deleteAction = menu.addAction(isFileItem ? tr("Delete All") : tr("Delete"));
    QAction* chosen = menu.exec(_tree->viewport()->mapToGlobal(pos));

    if (chosen == deleteAction)
    {
        emit variantDeleteRequested(sourceFile, variantIndex);
    }
    else if (chosen == captureAction)
    {
        promptCaptureVariant(sourceFile);
    }
    else if (chosen == setDefaultAction)
    {
        emit setDefaultVariantRequested(sourceFile, variantIndex);
    }
}

void MaterialVariantsPanel::onCaptureButtonClicked()
{
    promptCaptureVariant(_currentFile);
}

void MaterialVariantsPanel::onSetDefaultButtonClicked()
{
    if (!_sceneGraph || _currentFile.isEmpty())
        return;

    const int activeVariant = _sceneGraph->activeVariantForFile(_currentFile);
    if (activeVariant < 0)
        return;

    emit setDefaultVariantRequested(_currentFile, activeVariant);
}

void MaterialVariantsPanel::onDeleteButtonClicked()
{
    if (!_sceneGraph || _currentFile.isEmpty())
        return;

    const int activeVariant = _sceneGraph->activeVariantForFile(_currentFile);
    if (activeVariant < 0)
        return;

    emit variantDeleteRequested(_currentFile, activeVariant);
}

void MaterialVariantsPanel::promptCaptureVariant(const QString& sourceFile)
{
    if (sourceFile.isEmpty())
        return;

    const int existingCount = _sceneGraph ? _sceneGraph->variantDataForFile(sourceFile).variantNames.size() : 0;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Capture Variant"),
        tr("Variant name:"), QLineEdit::Normal,
        tr("Variant %1").arg(existingCount + 1), &ok).trimmed();
    if (ok && !name.isEmpty())
        emit captureVariantRequested(sourceFile, name);
}

void MaterialVariantsPanel::updateButtonStates()
{
    const bool hasFile = _sceneGraph && !_currentFile.isEmpty();
    const int activeVariant = hasFile ? _sceneGraph->activeVariantForFile(_currentFile) : -1;

    if (_captureButton)
        _captureButton->setEnabled(hasFile);
    if (_setDefaultButton)
        _setDefaultButton->setEnabled(hasFile && activeVariant >= 0);
    if (_deleteButton)
        _deleteButton->setEnabled(hasFile && activeVariant >= 0);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

QTreeWidgetItem* MaterialVariantsPanel::makeFileItem(const QString& sourceFile,
                                                      const QString& displayName) const
{
    auto* item = new QTreeWidgetItem();
    item->setText(0, displayName);
    item->setToolTip(0, sourceFile);
    item->setData(0, SourceFileRole,  sourceFile);
    item->setData(0, IsFileItemRole,  true);
    item->setData(0, VariantIndexRole, QVariant());
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
    QFont f = item->font(0);
    f.setBold(true);
    item->setFont(0, f);
    return item;
}

QTreeWidgetItem* MaterialVariantsPanel::makeVariantItem(const QString& label,
                                                         int variantIndex,
                                                         bool active) const
{
    auto* item = new QTreeWidgetItem();
    item->setText(0, label);
    item->setData(0, VariantIndexRole, variantIndex);
    item->setData(0, IsFileItemRole,   false);
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
    item->setIcon(0, active ? activeIcon() : inactiveIcon());
    return item;
}

void MaterialVariantsPanel::markActiveVariant(const QString& sourceFile, int variantIndex)
{
    for (int ti = 0; ti < _tree->topLevelItemCount(); ++ti)
    {
        QTreeWidgetItem* fileItem = _tree->topLevelItem(ti);
        if (fileItem->data(0, SourceFileRole).toString() != sourceFile)
            continue;

        for (int ci = 0; ci < fileItem->childCount(); ++ci)
        {
            QTreeWidgetItem* child = fileItem->child(ci);
            const int idx = child->data(0, VariantIndexRole).toInt();
            child->setIcon(0, (idx == variantIndex) ? activeIcon() : inactiveIcon());
        }
        break;
    }

    if (_sceneGraph)
        _sceneGraph->setActiveVariant(sourceFile, variantIndex);
}

QIcon MaterialVariantsPanel::activeIcon() const
{
    const QColor c = _tree ? _tree->palette().color(QPalette::Text)
                           : palette().color(QPalette::Text);
    return makeCircleIcon(true, c);
}

QIcon MaterialVariantsPanel::inactiveIcon() const
{
    QColor c = _tree ? _tree->palette().color(QPalette::Text)
                     : palette().color(QPalette::Text);
    c.setAlpha(160);
    return makeCircleIcon(false, c);
}
