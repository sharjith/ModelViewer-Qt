#include "CamerasPanel.h"
#include "ViewportWidget.h"
#include "SceneGraph.h"
#include "GltfCameraData.h"

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

namespace
{
QIcon makeCircleIcon(bool filled, const QColor& color)
{
    constexpr int size = 16;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 1.25));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QRectF(2.0, 2.0, size - 4.0, size - 4.0));
    if (filled)
    {
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(QRectF(5.1, 5.1, size - 10.2, size - 10.2));
    }
    return QIcon(pixmap);
}
} // namespace

// ---------------------------------------------------------------------------
// CamerasPanel
// ---------------------------------------------------------------------------

CamerasPanel::CamerasPanel(QWidget* parent)
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

    _captureViewButton = new QPushButton(tr("Capture View..."), this);
    _captureViewButton->setToolTip(tr("Save the current viewport camera pose as a new view"));
    _deleteButton = new QPushButton(tr("Delete"), this);
    _deleteButton->setToolTip(tr("Delete the last selected camera"));

    auto* buttonRow = new QHBoxLayout();
    buttonRow->setContentsMargins(8, 0, 8, 8);
    buttonRow->setSpacing(8);
    buttonRow->addWidget(_captureViewButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(_deleteButton);
    layout->addLayout(buttonRow);

    connect(_tree, &QTreeWidget::itemClicked,
            this,  &CamerasPanel::onItemClicked);
    connect(_tree, &QWidget::customContextMenuRequested,
            this, &CamerasPanel::onTreeContextMenuRequested);
    connect(_captureViewButton, &QPushButton::clicked, this, &CamerasPanel::onCaptureViewButtonClicked);
    connect(_deleteButton, &QPushButton::clicked, this, &CamerasPanel::onDeleteButtonClicked);

    updateButtonStates();
}

void CamerasPanel::setSceneGraph(SceneGraph* sg)
{
    _sceneGraph = sg;
}

void CamerasPanel::setViewportWidget(ViewportWidget* viewportWidget)
{
    _viewportWidget = viewportWidget;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void CamerasPanel::refresh()
{
    _tree->clear();

    // Camera indices can shift on add or delete, so a tracked target could
    // silently point at the wrong item after a data change. Safest is to
    // require a fresh click before Delete is usable again; this only fires
    // on actual data changes, not on ordinary activation clicks (those
    // don't call refresh()).
    _lastDeletableFile.clear();
    _lastDeletableIndex = -1;

    if (!_sceneGraph)
    {
        updateButtonStates();
        return;
    }

    // Determine the currently active camera so we can pre-mark it.
    const bool systemCamActive = !_viewportWidget || !_viewportWidget->isGltfCameraActive();
    const QString activeFile   = _viewportWidget ? _viewportWidget->activeGltfCameraFile()  : QString();
    const int     activeIndex  = _viewportWidget ? _viewportWidget->activeGltfCameraIndex() : -1;

    // --- System Camera item (always present) ---
    QTreeWidgetItem* sysItem = makeSystemCameraItem(systemCamActive);
    _tree->addTopLevelItem(sysItem);

    // --- Per-file glTF camera groups (includes the synthetic
    // capturedViewsSourceFileKey() bucket for user-captured views, pulled
    // to the front so it reads like a dedicated group right after System
    // Camera - same spot the old Bookmarks group used to occupy). ---
    QStringList files = _sceneGraph->filesWithGltfCameras();
    const QString capturedKey = capturedViewsSourceFileKey();
    if (files.removeOne(capturedKey))
        files.prepend(capturedKey);

    for (const QString& sourceFile : files)
    {
        const GltfCameraData cd = _sceneGraph->gltfCameraDataForFile(sourceFile);
        if (cd.isEmpty())
            continue;

        const QString displayName = (sourceFile == capturedKey)
            ? tr("Captured Views")
            : QFileInfo(sourceFile).fileName();
        QTreeWidgetItem* fileItem = makeFileItem(sourceFile, displayName);
        _tree->addTopLevelItem(fileItem);

        for (int i = 0; i < cd.cameras.size(); ++i)
        {
            const bool active = !systemCamActive
                                && sourceFile == activeFile
                                && i == activeIndex;
            fileItem->addChild(makeCameraItem(cd.cameras[i].name, sourceFile, i, active));
        }

        fileItem->setExpanded(true);
    }

    updateButtonStates();
}

void CamerasPanel::setDetachedOverlayMode(bool enabled)
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

void CamerasPanel::refreshDetachedOverlayTheme()
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

void CamerasPanel::paintEvent(QPaintEvent* event)
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

void CamerasPanel::onItemClicked(QTreeWidgetItem* item, int /*column*/)
{
    if (!item)
        return;

    // File-group items (including "Captured Views") are not clickable
    if (item->data(0, IsFileItemRole).toBool())
        return;

    const bool isSystemCam = item->data(0, IsSystemCamRole).toBool();

    if (isSystemCam)
    {
        markActive(QString(), -1, /*isSystemCam=*/true);
        // System Camera isn't deletable - clear any previously-tracked target.
        _lastDeletableFile.clear();
        _lastDeletableIndex = -1;
        updateButtonStates();
        emit systemCameraRequested();
        return;
    }

    // glTF camera item (authored or captured): SourceFileRole is stored
    // directly on camera items too.
    const QString sourceFile  = item->data(0, SourceFileRole).toString();
    const int     cameraIndex = item->data(0, CameraIndexRole).toInt();

    if (sourceFile.isEmpty() || cameraIndex < 0)
        return;

    markActive(sourceFile, cameraIndex, /*isSystemCam=*/false);
    _lastDeletableFile = sourceFile;
    _lastDeletableIndex = cameraIndex;
    updateButtonStates();
    emit gltfCameraActivated(sourceFile, cameraIndex);
}

void CamerasPanel::onTreeContextMenuRequested(const QPoint& pos)
{
    if (!_tree)
        return;

    QTreeWidgetItem* item = _tree->itemAt(pos);
    if (!item)
        return;

    if (item->data(0, IsSystemCamRole).toBool())
    {
        QMenu menu(this);
        QAction* captureAction = menu.addAction(tr("Capture Camera View..."));
        QAction* chosen = menu.exec(_tree->viewport()->mapToGlobal(pos));
        if (chosen == captureAction)
            promptCaptureCameraView();
        return;
    }

    const bool isFileItem = item->data(0, IsFileItemRole).toBool();
    const QString sourceFile = isFileItem
        ? item->data(0, SourceFileRole).toString()
        : (item->parent() ? item->parent()->data(0, SourceFileRole).toString() : QString());
    const int cameraIndex = isFileItem ? -1 : item->data(0, CameraIndexRole).toInt();

    if (sourceFile.isEmpty())
        return;

    QMenu menu(this);
    QAction* deleteAction = menu.addAction(isFileItem ? tr("Delete All") : tr("Delete"));
    QAction* chosen = menu.exec(_tree->viewport()->mapToGlobal(pos));
    if (chosen == deleteAction)
        emit gltfCameraDeleteRequested(sourceFile, cameraIndex);
}

void CamerasPanel::onCaptureViewButtonClicked()
{
    promptCaptureCameraView();
}

void CamerasPanel::onDeleteButtonClicked()
{
    if (_lastDeletableFile.isEmpty() || _lastDeletableIndex < 0)
        return;
    emit gltfCameraDeleteRequested(_lastDeletableFile, _lastDeletableIndex);
}

void CamerasPanel::promptCaptureCameraView()
{
    const int existingCount = _sceneGraph
        ? _sceneGraph->gltfCameraDataForFile(capturedViewsSourceFileKey()).cameras.size()
        : 0;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Capture Camera"),
        tr("View name:"), QLineEdit::Normal,
        tr("View %1").arg(existingCount + 1), &ok).trimmed();
    if (ok && !name.isEmpty())
        emit captureViewRequested(name);
}

void CamerasPanel::updateButtonStates()
{
    if (_captureViewButton)
        _captureViewButton->setEnabled(_sceneGraph != nullptr && _viewportWidget != nullptr);
    if (_deleteButton)
        _deleteButton->setEnabled(!_lastDeletableFile.isEmpty() && _lastDeletableIndex >= 0);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

QTreeWidgetItem* CamerasPanel::makeSystemCameraItem(bool active) const
{
    auto* item = new QTreeWidgetItem();
    item->setText(0, tr("System Camera"));
    item->setData(0, IsSystemCamRole, true);
    item->setData(0, IsFileItemRole,  false);
    item->setData(0, CameraIndexRole, -1);
    item->setData(0, SourceFileRole,  QString());
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
    item->setIcon(0, active ? activeIcon() : inactiveIcon());
    QFont f = item->font(0);
    f.setItalic(true);
    item->setFont(0, f);
    return item;
}

QTreeWidgetItem* CamerasPanel::makeFileItem(const QString& sourceFile,
                                             const QString& displayName) const
{
    auto* item = new QTreeWidgetItem();
    item->setText(0, displayName);
    item->setToolTip(0, sourceFile);
    item->setData(0, SourceFileRole,  sourceFile);
    item->setData(0, IsFileItemRole,  true);
    item->setData(0, IsSystemCamRole, false);
    item->setData(0, CameraIndexRole, QVariant());
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
    QFont f = item->font(0);
    f.setBold(true);
    item->setFont(0, f);
    return item;
}

QTreeWidgetItem* CamerasPanel::makeCameraItem(const QString& label,
                                               const QString& sourceFile,
                                               int cameraIndex,
                                               bool active) const
{
    auto* item = new QTreeWidgetItem();
    item->setText(0, label);
    item->setData(0, SourceFileRole,  sourceFile);
    item->setData(0, CameraIndexRole, cameraIndex);
    item->setData(0, IsFileItemRole,  false);
    item->setData(0, IsSystemCamRole, false);
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
    item->setIcon(0, active ? activeIcon() : inactiveIcon());
    return item;
}

void CamerasPanel::markActive(const QString& sourceFile, int cameraIndex, bool isSystemCam)
{
    for (int ti = 0; ti < _tree->topLevelItemCount(); ++ti)
    {
        QTreeWidgetItem* topItem = _tree->topLevelItem(ti);

        // System Camera top-level item
        if (topItem->data(0, IsSystemCamRole).toBool())
        {
            topItem->setIcon(0, isSystemCam ? activeIcon() : inactiveIcon());
            continue;
        }

        // File-group item — update its children
        if (topItem->data(0, IsFileItemRole).toBool())
        {
            const QString fileKey = topItem->data(0, SourceFileRole).toString();
            for (int ci = 0; ci < topItem->childCount(); ++ci)
            {
                QTreeWidgetItem* child = topItem->child(ci);
                const bool active = !isSystemCam
                                    && fileKey == sourceFile
                                    && child->data(0, CameraIndexRole).toInt() == cameraIndex;
                child->setIcon(0, active ? activeIcon() : inactiveIcon());
            }
        }
    }
}

QIcon CamerasPanel::activeIcon() const
{
    const QColor c = _tree ? _tree->palette().color(QPalette::Text)
                           : palette().color(QPalette::Text);
    return makeCircleIcon(true, c);
}

QIcon CamerasPanel::inactiveIcon() const
{
    QColor c = _tree ? _tree->palette().color(QPalette::Text)
                     : palette().color(QPalette::Text);
    c.setAlpha(160);
    return makeCircleIcon(false, c);
}
