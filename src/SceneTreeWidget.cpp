#include "SceneTreeWidget.h"
#include "SceneGraph.h"
#include "SceneNode.h"
#include "ViewportWidget.h"
#include "RenderableMesh.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QProxyStyle>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyleOption>
#include <QStyledItemDelegate>
#include <QScrollBar>
#include <QWheelEvent>
#include <algorithm>
#include <limits>

// ---------------------------------------------------------------------------
// Icon helpers
// ---------------------------------------------------------------------------

static QPixmap makeGreyscale(const QPixmap& src)
{
    QImage img = src.toImage().convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y)
    {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x)
        {
            const int g = qGray(line[x]);
            line[x] = qRgba(g, g, g, qAlpha(line[x]));
        }
    }
    return QPixmap::fromImage(img);
}

// Lazy-loaded once (after QApplication exists).
// To swap icon sets, change the resource paths here.
struct TreeIcons
{
    QPixmap fileNormal,     fileGrey;
    QPixmap assemblyNormal, assemblyGrey;
    QPixmap meshNormal,     meshGrey;

    TreeIcons()
    {
        QPixmap f(":/icons/res/document-root.png");
        QPixmap a(":/icons/res/assembly.png");
        QPixmap m(":/icons/res/mesh.png");
        fileNormal     = f; fileGrey     = makeGreyscale(f);
        assemblyNormal = a; assemblyGrey = makeGreyscale(a);
        meshNormal     = m; meshGrey     = makeGreyscale(m);
    }
};

static const TreeIcons& treeIcons()
{
    static TreeIcons inst;
    return inst;
}

// ---------------------------------------------------------------------------
// PlusMinusStyle
//
// Replaces the platform arrow branch indicator with a classic +/- box.
// ---------------------------------------------------------------------------
class PlusMinusStyle : public QProxyStyle
{
public:
    using QProxyStyle::QProxyStyle;

    void drawPrimitive(PrimitiveElement    pe,
                       const QStyleOption* opt,
                       QPainter*           p,
                       const QWidget*      w) const override
    {
        if (pe == PE_IndicatorBranch && (opt->state & State_Children))
        {
            const int sz = 11;
            QRect box = QRect(opt->rect.center() - QPoint(sz / 2, sz / 2),
                              QSize(sz, sz));
            const bool detachedOverlay =
                (w && w->property("detachedOverlayMode").toBool());
            const bool lightText =
                detachedOverlay && w ? w->property("overlayViewerLightText").toBool() : false;
            const QColor textColor = detachedOverlay
                ? (lightText ? QColor(255, 255, 255) : QColor(0, 0, 0))
                : opt->palette.text().color();
            const QColor baseColor = detachedOverlay
                ? (lightText ? QColor(24, 24, 24, 225) : QColor(255, 255, 255, 225))
                : opt->palette.base().color();
            p->save();
            p->setPen(QPen(textColor, 1));
            p->setBrush(baseColor);
            p->drawRect(box);
            // Horizontal bar
            p->drawLine(box.left() + 2,  box.center().y(),
                        box.right() - 2, box.center().y());
            // Vertical bar only when closed (+)
            if (!(opt->state & State_Open))
                p->drawLine(box.center().x(), box.top() + 2,
                            box.center().x(), box.bottom() - 2);
            p->restore();
            return;
        }
        QProxyStyle::drawPrimitive(pe, opt, p, w);
    }
};

class OverlayTreeItemDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);

        const bool detachedOverlay =
            (opt.widget && opt.widget->property("detachedOverlayMode").toBool());
        const bool lightText =
            detachedOverlay && opt.widget ? opt.widget->property("overlayViewerLightText").toBool() : false;
        const QColor detachedTextColor = lightText ? QColor(255, 255, 255) : QColor(0, 0, 0);
        const QColor detachedHighlightTextColor = detachedTextColor;

        if (detachedOverlay)
        {
            opt.palette.setColor(QPalette::Text, detachedTextColor);
            opt.palette.setColor(QPalette::WindowText, detachedTextColor);
            opt.palette.setColor(QPalette::ButtonText, detachedTextColor);
            opt.palette.setColor(QPalette::HighlightedText, detachedHighlightTextColor);
        }

        if (!detachedOverlay || !(opt.features & QStyleOptionViewItem::HasCheckIndicator))
        {
            QStyledItemDelegate::paint(painter, opt, index);
            return;
        }

        QStyleOptionViewItem contentOpt(opt);
        contentOpt.features &= ~QStyleOptionViewItem::HasCheckIndicator;
        QStyledItemDelegate::paint(painter, contentOpt, index);

        QStyle* style = opt.widget ? opt.widget->style() : QApplication::style();
        const QRect indicatorRect = style->subElementRect(QStyle::SE_ItemViewItemCheckIndicator,
                                                          &opt, opt.widget);
        if (!indicatorRect.isValid())
            return;

        const QColor textColor = (opt.state & QStyle::State_Selected)
            ? opt.palette.color(QPalette::HighlightedText)
            : opt.palette.color(QPalette::Text);
        const bool darkText = textColor.lightnessF() < 0.5;
        const QColor boxFill = darkText
            ? QColor(255, 255, 255, 225)
            : QColor(28, 28, 28, 225);
        const QColor boxBorder = darkText
            ? QColor(0, 0, 0, 100)
            : QColor(255, 255, 255, 110);

        const QRect box = indicatorRect.adjusted(1, 1, -1, -1);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(boxBorder, 1.0));
        painter->setBrush(boxFill);
        painter->drawRoundedRect(box, 2.0, 2.0);

        const QVariant checkData = index.data(Qt::CheckStateRole);
        const Qt::CheckState state = static_cast<Qt::CheckState>(checkData.toInt());
        painter->setPen(QPen(textColor, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

        if (state == Qt::Checked)
        {
            const QPoint p1(box.left() + 3, box.center().y());
            const QPoint p2(box.center().x() - 1, box.bottom() - 3);
            const QPoint p3(box.right() - 2, box.top() + 3);
            painter->drawLine(p1, p2);
            painter->drawLine(p2, p3);
        }
        else if (state == Qt::PartiallyChecked)
        {
            painter->drawLine(box.left() + 3, box.center().y(),
                              box.right() - 3, box.center().y());
        }

        painter->restore();
    }
};

// ---------------------------------------------------------------------------
// SceneTreeWidget
// ---------------------------------------------------------------------------

SceneTreeWidget::SceneTreeWidget(QWidget* parent)
    : QTreeWidget(parent)
{
    _rebuildTimer = new QTimer(this);
    _rebuildTimer->setSingleShot(true);
    connect(_rebuildTimer, &QTimer::timeout,
            this, &SceneTreeWidget::processRebuildBatch);

    setHeaderHidden(true);
    // By default QTreeWidget stretches the last section to fill the viewport,
    // which overrides ResizeToContents and prevents the horizontal scrollbar
    // from ever appearing.  Disable stretch first, then let the column size
    // itself to its content so overflow triggers the scrollbar.
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setEditTriggers(QAbstractItemView::DoubleClicked
                  | QAbstractItemView::EditKeyPressed);
    setAnimated(true);
    setRootIsDecorated(true);
    setUniformRowHeights(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setStyle(new PlusMinusStyle(style()));              
    setItemDelegate(new OverlayTreeItemDelegate(this));
    setProperty("detachedOverlayMode", false);
    viewport()->setProperty("detachedOverlayMode", false);

    // --- item changed (check-state or text) ----------------------------------
    connect(this, &QTreeWidget::itemChanged,
            this, &SceneTreeWidget::onItemChanged);

    // --- selection changed ---------------------------------------------------
    connect(this, &QTreeWidget::itemSelectionChanged,
            this, &SceneTreeWidget::onItemSelectionChanged);

    // --- rename detection via delegate close-editor --------------------------
    connect(itemDelegate(), &QAbstractItemDelegate::commitData,
            this, [this](QWidget*) {
                _inRename = true;   // next itemChanged is from the delegate
            });

    connect(itemDelegate(), &QAbstractItemDelegate::closeEditor,
            this, [this](QWidget* editor, QAbstractItemDelegate::EndEditHint) {
                _inRename = false;

                QTreeWidgetItem* cur = currentItem();
                if (!cur || !cur->data(0, IsLeafRole).toBool()) return;

                auto* le = qobject_cast<QLineEdit*>(editor);
                if (!le) return;

                const QString newName = le->text().trimmed();
                if (newName.isEmpty()) return;

                const QUuid uuid = cur->data(0, MeshUuidRole).value<QUuid>();
                emit meshRenamed(uuid, newName);
            });
}

// ---------------------------------------------------------------------------
// Setters
// ---------------------------------------------------------------------------

void SceneTreeWidget::setSceneGraph(SceneGraph* sg)
{
    if (_sceneGraph)
        disconnect(_sceneGraph, &SceneGraph::structureChanged,
                   this,        &SceneTreeWidget::rebuild);

    _sceneGraph = sg;

    if (_sceneGraph)
        connect(_sceneGraph, &SceneGraph::structureChanged,
                this,        &SceneTreeWidget::rebuild);
}

void SceneTreeWidget::setViewportWidget(ViewportWidget* viewportWidget)
{
    _viewportWidget = viewportWidget;
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

QList<QUuid> SceneTreeWidget::selectedMeshUuids() const
{
    QList<QUuid> result;
    for (QTreeWidgetItem* item : selectedItems())
    {
        if (item->data(0, IsLeafRole).toBool())
        {
            result << item->data(0, MeshUuidRole).value<QUuid>();
        }
        else
        {
            QList<QTreeWidgetItem*> leaves;
            collectLeaves(item, leaves);
            for (QTreeWidgetItem* leaf : leaves)
                result << leaf->data(0, MeshUuidRole).value<QUuid>();
        }
    }

    QList<QUuid> deduped;
    QSet<QUuid> seen;
    for (const QUuid& uuid : std::as_const(result))
    {
        if (!seen.contains(uuid))
        {
            seen.insert(uuid);
            deduped << uuid;
        }
    }
    return deduped;
}

bool SceneTreeWidget::hasMeshSelection() const
{
    for (QTreeWidgetItem* item : selectedItems())
    {
        if (item->data(0, IsLeafRole).toBool())
            return true;
    }
    return false;
}

int SceneTreeWidget::meshCount() const
{
    return _uuidToLeaf.size();
}

void SceneTreeWidget::setSelectionByIndices(const QSet<int>& indices)
{
    if (!_viewportWidget) return;
    QSet<QUuid> uuids;
    for (int idx : indices)
    {
        QUuid u = _viewportWidget->getUuidByIndex(idx);
        if (!u.isNull()) uuids.insert(u);
    }
    setSelectionByUuids(uuids);
}

void SceneTreeWidget::setSelectionByUuids(const QSet<QUuid>& uuids)
{
    _updatingTree = true;
    blockSignals(true);
    clearSelection();
    for (const QUuid& uuid : uuids)
    {
        auto it = _uuidToLeaf.find(uuid);
        if (it != _uuidToLeaf.end())
            it.value()->setSelected(true);
    }

    for (int i = 0; i < topLevelItemCount(); ++i)
        refreshSelectionClosureBottomUp(topLevelItem(i));

    blockSignals(false);

    _prevSelection.clear();
    for (QTreeWidgetItem* it : selectedItems())
        _prevSelection.insert(it);

    _updatingTree = false;

	// center the first selected item in the viewport for better visibility
    scrollFirstSelectedToCenter();
}

std::vector<int> SceneTreeWidget::getSelectedIndices() const
{
    std::vector<int> ids;
    if (!_viewportWidget) return ids;
    for (const QUuid& uuid : selectedMeshUuids())
    {
        int idx = _viewportWidget->getIndexByUuid(uuid);
        if (idx >= 0) ids.push_back(idx);
    }
    return ids;
}

QUuid SceneTreeWidget::currentMeshUuid() const
{
    QTreeWidgetItem* cur = currentItem();
    if (!cur || !cur->data(0, IsLeafRole).toBool())
        return QUuid{};
    return cur->data(0, MeshUuidRole).value<QUuid>();
}

void SceneTreeWidget::clearMeshSelection()
{
    _updatingTree = true;
    blockSignals(true);
    clearSelection();
    blockSignals(false);

    _prevSelection.clear();
    for (QTreeWidgetItem* it : selectedItems())
        _prevSelection.insert(it);

    _updatingTree = false;
}

// ---------------------------------------------------------------------------
// Visibility
// ---------------------------------------------------------------------------

void SceneTreeWidget::setVisibilityByUuids(const QSet<QUuid>& visibleUuids)
{
    _updatingTree = true;
    {
        QList<QTreeWidgetItem*> expandedItems;
        expandedItems.reserve(topLevelItemCount() * 4);
        for (int i = 0; i < topLevelItemCount(); ++i)
            collectExpandedItems(topLevelItem(i), expandedItems);

        const int headerWidth = columnWidth(0);
        const auto previousResizeMode = header()->sectionResizeMode(0);

        setUpdatesEnabled(false);
        viewport()->setUpdatesEnabled(false);
        header()->setSectionResizeMode(0, QHeaderView::Fixed);
        if (headerWidth > 0)
            setColumnWidth(0, headerWidth);

        const QSignalBlocker blocker(this);

        for (auto it = expandedItems.crbegin(); it != expandedItems.crend(); ++it)
            (*it)->setExpanded(false);

        for (auto it = _uuidToLeaf.begin(); it != _uuidToLeaf.end(); ++it)
        {
            Qt::CheckState cs = visibleUuids.contains(it.key())
                                ? Qt::Checked : Qt::Unchecked;
            it.value()->setCheckState(0, cs);
            updateItemIcon(it.value());
        }

        // Update tristate and icons on all assembly nodes bottom-up.
        refreshAllAssemblyStates();

        for (QTreeWidgetItem* item : expandedItems)
            item->setExpanded(true);

        header()->setSectionResizeMode(0, previousResizeMode);
        if (previousResizeMode == QHeaderView::Fixed && headerWidth > 0)
            setColumnWidth(0, headerWidth);

        setUpdatesEnabled(true);
        viewport()->setUpdatesEnabled(true);
        viewport()->update();
    }
    _updatingTree = false;
}

void SceneTreeWidget::setVisibilityDelta(const QSet<QUuid>& changedUuids,
                                         const QSet<QUuid>& visibleUuids)
{
    if (changedUuids.isEmpty())
    {
        setVisibilityByUuids(visibleUuids);
        return;
    }

    _updatingTree = true;
    {
        viewport()->setUpdatesEnabled(false);
        const QSignalBlocker blocker(this);

        QSet<QTreeWidgetItem*> ancestors;
        bool anyChanged = false;

        for (const QUuid& uuid : changedUuids)
        {
            auto it = _uuidToLeaf.find(uuid);
            if (it == _uuidToLeaf.end())
                continue;

            QTreeWidgetItem* leaf = it.value();
            const Qt::CheckState targetState =
                visibleUuids.contains(uuid) ? Qt::Checked : Qt::Unchecked;

            if (leaf->checkState(0) != targetState)
            {
                leaf->setCheckState(0, targetState);
                anyChanged = true;
            }

            updateItemIcon(leaf);
            for (QTreeWidgetItem* parent = leaf->parent(); parent; parent = parent->parent())
                ancestors.insert(parent);
        }

        if (anyChanged && !ancestors.isEmpty())
        {
            QList<QTreeWidgetItem*> orderedAncestors = ancestors.values();
            std::sort(orderedAncestors.begin(), orderedAncestors.end(),
                      [](QTreeWidgetItem* a, QTreeWidgetItem* b) {
                          auto depthOf = [](QTreeWidgetItem* item) {
                              int depth = 0;
                              for (QTreeWidgetItem* p = item; p; p = p->parent())
                                  ++depth;
                              return depth;
                          };
                          return depthOf(a) > depthOf(b);
                      });

            for (QTreeWidgetItem* item : orderedAncestors)
            {
                item->setCheckState(0, aggregateChildCheckState(item));
                updateItemIcon(item);
            }
        }

        viewport()->setUpdatesEnabled(true);
        viewport()->update();
    }
    _updatingTree = false;
}

QSet<QUuid> SceneTreeWidget::getVisibleUuids() const
{
    QSet<QUuid> result;
    for (auto it = _uuidToLeaf.constBegin(); it != _uuidToLeaf.constEnd(); ++it)
    {
        if (it.value()->checkState(0) == Qt::Checked)
            result.insert(it.key());
    }
    return result;
}

std::vector<int> SceneTreeWidget::getVisibleIndices() const
{
    std::vector<int> ids;
    if (!_viewportWidget) return ids;
    for (auto it = _uuidToLeaf.constBegin(); it != _uuidToLeaf.constEnd(); ++it)
    {
        if (it.value()->checkState(0) == Qt::Checked)
        {
            int idx = _viewportWidget->getIndexByUuid(it.key());
            if (idx >= 0) ids.push_back(idx);
        }
    }
    // setDisplayList uses std::set_difference which requires a sorted range
    std::sort(ids.begin(), ids.end());
    return ids;
}

int SceneTreeWidget::visibleMeshCount() const
{
    int count = 0;
    for (auto it = _uuidToLeaf.constBegin(); it != _uuidToLeaf.constEnd(); ++it)
        if (it.value()->checkState(0) == Qt::Checked) ++count;
    return count;
}

// ---------------------------------------------------------------------------
// Filter
// ---------------------------------------------------------------------------

void SceneTreeWidget::filterItems(const QString& filter)
{
    _updatingTree = true;
    viewport()->setUpdatesEnabled(false);
    {
        const QSignalBlocker blocker(this);
        clearSelection();

        if (filter.isEmpty())
        {
            for (int i = 0; i < topLevelItemCount(); ++i)
                showSubtree(topLevelItem(i));
        }
        else
        {
            const QString lowerFilter = filter.toLower();
            bool anySubstringMatch = false;
            int bestSubstringRank = 0;
            for (int i = 0; i < topLevelItemCount(); ++i)
                applySubstringFilter(topLevelItem(i),
                                     lowerFilter,
                                     false,
                                     anySubstringMatch,
                                     bestSubstringRank);

            if (anySubstringMatch && bestSubstringRank > 0)
            {
                for (int i = 0; i < topLevelItemCount(); ++i)
                    selectMatchesAtRank(topLevelItem(i), lowerFilter, bestSubstringRank);
            }

            if (!anySubstringMatch)
            {
                QTreeWidgetItem* bestItem = nullptr;
                int bestScore = std::numeric_limits<int>::max();

                for (int i = 0; i < topLevelItemCount(); ++i)
                {
                    QTreeWidgetItem* candidate =
                        findBestFuzzyMatch(topLevelItem(i), lowerFilter, bestScore);
                    if (candidate)
                        bestItem = candidate;
                }

                if (bestItem && bestScore <= fuzzyThreshold(lowerFilter))
                {
                    for (QTreeWidgetItem* p = bestItem->parent(); p; p = p->parent())
                    {
                        p->setHidden(false);
                        p->setExpanded(true);
                    }
                    if (bestItem->data(0, IsLeafRole).toBool())
                    {
                        bestItem->setHidden(false);
                    }
                    else
                    {
                        showSubtree(bestItem);
                    }
                    selectSearchMatch(bestItem);
                }
            }
        }
    }

    viewport()->setUpdatesEnabled(true);
    viewport()->update();

    for (int i = 0; i < topLevelItemCount(); ++i)
        refreshSelectionClosureBottomUp(topLevelItem(i));

    _prevSelection.clear();
    for (QTreeWidgetItem* it : selectedItems())
        _prevSelection.insert(it);

    _updatingTree = false;
    emit selectionUpdated();
}

// ---------------------------------------------------------------------------
// Misc
// ---------------------------------------------------------------------------

QPoint SceneTreeWidget::mapMenuToGlobal(const QPoint& localPos) const
{
    return viewport()->mapToGlobal(localPos);
}

bool SceneTreeWidget::isAssemblyAt(const QPoint& localPos) const
{
    QTreeWidgetItem* item = itemAt(localPos);
    return item && !item->data(0, IsLeafRole).toBool();
}

bool SceneTreeWidget::hasChildrenAt(const QPoint& localPos) const
{
    QTreeWidgetItem* item = itemAt(localPos);
    return item && item->childCount() > 0;
}

void SceneTreeWidget::ensureAssemblySelectionAt(const QPoint& localPos)
{
    QTreeWidgetItem* item = itemAt(localPos);
    if (!item || item->data(0, IsLeafRole).toBool())
        return;

    if (!item->isSelected())
    {
        _updatingTree = true;
        blockSignals(true);
        clearSelection();
        item->setSelected(true);
        setCurrentItem(item);
        blockSignals(false);

        _prevSelection.clear();
        for (QTreeWidgetItem* it : selectedItems())
            _prevSelection.insert(it);

        _updatingTree = false;
        emit selectionUpdated();
    }
}

void SceneTreeWidget::expandOneLevelAt(const QPoint& localPos)
{
    expandOneLevel(itemAt(localPos));
}

void SceneTreeWidget::expandSubtreeAt(const QPoint& localPos)
{
    expandSubtree(itemAt(localPos));
}

void SceneTreeWidget::collapseAllBelowAt(const QPoint& localPos)
{
    collapseAllBelow(itemAt(localPos));
}

void SceneTreeWidget::scrollFirstSelectedToCenter()
{
    const QList<QTreeWidgetItem*> sel = selectedItems();
    if (sel.isEmpty())
        return;

    // Preserve horizontal scroll so the view doesn't "snap" sideways
    QScrollBar* hbar = horizontalScrollBar();
    const int hBefore = hbar ? hbar->value() : 0;

    // This centers the item vertically, but may also "helpfully" center horizontally
    scrollToItem(sel.first(), QAbstractItemView::PositionAtCenter);

    // Restore horizontal position to avoid sideways jump
    if (hbar)
        hbar->setValue(hBefore);
}

void SceneTreeWidget::setDetachedOverlayMode(bool enabled)
{
    if (_detachedOverlayMode == enabled)
        return;

      if (enabled)
      {
          _savedStyleSheet = styleSheet();
          _savedTreePalette = palette();
          _savedViewportPalette = viewport()->palette();
        _savedAutoFillBackground = autoFillBackground();
        _savedViewportAutoFillBackground = viewport()->autoFillBackground();

        QPalette treePalette = _savedTreePalette;
          QColor base = treePalette.color(QPalette::Base);
          QColor alternate = treePalette.color(QPalette::AlternateBase);
          const bool lightText = property("overlayViewerLightText").toBool();
          const QColor textColor = lightText
              ? QColor(255, 255, 255)
              : QColor(0, 0, 0);
          base.setAlpha(0);
          alternate.setAlpha(0);
          treePalette.setColor(QPalette::Base, base);
          treePalette.setColor(QPalette::AlternateBase, alternate);
          treePalette.setColor(QPalette::Text, textColor);
          treePalette.setColor(QPalette::WindowText, textColor);
          treePalette.setColor(QPalette::ButtonText, textColor);
          treePalette.setColor(QPalette::HighlightedText, textColor);

          setPalette(treePalette);
          viewport()->setPalette(treePalette);
          setAutoFillBackground(false);
          viewport()->setAutoFillBackground(false);
          setAttribute(Qt::WA_NoSystemBackground, true);
          viewport()->setAttribute(Qt::WA_NoSystemBackground, true);
          viewport()->setAttribute(Qt::WA_StyledBackground, false);
          setStyleSheet(QString());
      }
    else
    {
        setStyleSheet(_savedStyleSheet);
        setPalette(_savedTreePalette);
        viewport()->setPalette(_savedViewportPalette);
        setAutoFillBackground(_savedAutoFillBackground);
        viewport()->setAutoFillBackground(_savedViewportAutoFillBackground);
        setAttribute(Qt::WA_NoSystemBackground, false);
        viewport()->setAttribute(Qt::WA_NoSystemBackground, false);
        viewport()->setAttribute(Qt::WA_StyledBackground, false);
    }

      _detachedOverlayMode = enabled;
      setProperty("detachedOverlayMode", enabled);
      viewport()->setProperty("detachedOverlayMode", enabled);
      viewport()->update();
      update();
  }

void SceneTreeWidget::paintEvent(QPaintEvent* event)
{
    if (_detachedOverlayMode)
    {
        QPainter painter(viewport());
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(event->rect(), _detachedOverlayFillColor);
    }

    QTreeWidget::paintEvent(event);
}

// ---------------------------------------------------------------------------
// rebuild
// ---------------------------------------------------------------------------

void SceneTreeWidget::rebuild()
{
    if (!_sceneGraph || !_viewportWidget) return;

    if (_rebuildTimer->isActive())
        _rebuildTimer->stop();

    _pendingBuildTasks.clear();
    _pendingVisibleUuids.clear();
    _pendingSelectedUuids.clear();
    _pendingOldUuids.clear();

    const bool hadItems = !_uuidToLeaf.isEmpty();
    for (auto it = _uuidToLeaf.constBegin(); it != _uuidToLeaf.constEnd(); ++it)
    {
        _pendingOldUuids.insert(it.key());
        if (it.value()->checkState(0) == Qt::Checked)
            _pendingVisibleUuids.insert(it.key());
        if (it.value()->isSelected())
            _pendingSelectedUuids.insert(it.key());
    }

    if (!hadItems)
    {
        const std::vector<int> displayedIds = _viewportWidget->getDisplayedObjectsIds();
        for (int idx : displayedIds)
        {
            QUuid uuid = _viewportWidget->getUuidByIndex(idx);
            if (!uuid.isNull())
                _pendingVisibleUuids.insert(uuid);
        }
    }

    clear();
    _uuidToLeaf.clear();
    _prevSelection.clear();

    SceneNode* root = _sceneGraph->root();
    if (!root)
        return;

    _updatingTree = true;
    blockSignals(true);
    _rebuildInProgress = true;

    for (SceneNode* fileNode : root->children)
    {
        if (!nodeHasMeshes(fileNode))
            continue;
        enqueueRebuildTasks(nullptr, fileNode, true);
    }

    // Delay the first batch slightly so the fit animation (5 ms timer) can
    // fire a few times before tree population starts competing on the UI thread.
    // Subsequent batches continue at 0 ms in processRebuildBatch().
    _rebuildTimer->start(100);
}

// ---------------------------------------------------------------------------
// Key press
// ---------------------------------------------------------------------------

void SceneTreeWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space)
    {
        // Toggle visibility of all selected mesh leaves
        QList<QTreeWidgetItem*> sel = selectedItems();
        if (!sel.isEmpty())
        {
            _updatingTree = true;
            blockSignals(true);
            for (QTreeWidgetItem* item : sel)
            {
                if (!item->data(0, IsLeafRole).toBool()) continue;
                Qt::CheckState newState =
                    (item->checkState(0) == Qt::Checked) ? Qt::Unchecked : Qt::Checked;
                item->setCheckState(0, newState);
                updateItemIcon(item);
                refreshCheckUpward(item->parent());
            }
            blockSignals(false);
            _updatingTree = false;
            emit meshVisibilityChanged();
        }
    }
    else
    {
        QTreeWidget::keyPressEvent(event);
    }
}

// ---------------------------------------------------------------------------
// Mouse press — toggle-deselect on re-click
// ---------------------------------------------------------------------------

static bool isDescendantOf(QTreeWidgetItem* root, QTreeWidgetItem* item)
{
    for (QTreeWidgetItem* p = item; p; p = p->parent())
        if (p == root) return true;
    return false;
}

void SceneTreeWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton &&
        !(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)))
    {
        QTreeWidgetItem* clicked = itemAt(event->pos());
        if (clicked && clicked->isSelected())
        {
            // Allow toggle-off if ALL currently selected items lie within clicked's subtree
            const QList<QTreeWidgetItem*> sel = selectedItems();
            bool onlyWithinSubtree = true;
            for (QTreeWidgetItem* s : sel)
            {
                if (!isDescendantOf(clicked, s))
                {
                    onlyWithinSubtree = false;
                    break;
                }
            }

            if (onlyWithinSubtree)
            {
                const Qt::CheckState csBefore = clicked->checkState(0);
                const bool expandedBefore = clicked->isExpanded();

                QTreeWidget::mousePressEvent(event);

                if (clicked->checkState(0) == csBefore &&
                    clicked->isExpanded() == expandedBefore &&
                    clicked->isSelected())
                {
                    clearSelection();
                }
                return;
            }
        }
    }

    QTreeWidget::mousePressEvent(event);
}

void SceneTreeWidget::mouseReleaseEvent(QMouseEvent* event)
{
    QTreeWidget::mouseReleaseEvent(event);
}

void SceneTreeWidget::wheelEvent(QWheelEvent* event)
{
    event->ignore();
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------

void SceneTreeWidget::onItemChanged(QTreeWidgetItem* item, int /*column*/)
{
    if (_updatingTree || _inRename || !item) return;

    const bool isLeaf = item->data(0, IsLeafRole).toBool();

    if (isLeaf)
    {
        // Leaf check-state changed → update icon, refresh parent tristate.
        // _updatingTree must be set BEFORE updateItemIcon so that the setIcon()
        // call inside it does not re-fire onItemChanged (infinite loop).
        _updatingTree = true;
        blockSignals(true);
        updateItemIcon(item);
        refreshCheckUpward(item->parent());
        blockSignals(false);
        _updatingTree = false;
        emit meshVisibilityChanged();
    }
    else
    {
        // Assembly / file node check-state → propagate to all descendant leaves,
        // then refresh all ancestors' tristate states.
        Qt::CheckState cs = item->checkState(0);
        if (cs == Qt::PartiallyChecked) return;  // partial is set programmatically only

        _updatingTree = true;
        blockSignals(true);
        propagateCheckDown(item, cs);
        updateItemIcon(item);               // assembly's own icon after propagation
        refreshCheckUpward(item->parent()); // update ancestors
        blockSignals(false);
        _updatingTree = false;
        emit meshVisibilityChanged();
    }
}

void SceneTreeWidget::onItemSelectionChanged()
{
    if (_updatingTree)
        return;

    // Snapshot current selection
    const QList<QTreeWidgetItem*> selList = selectedItems();
    QSet<QTreeWidgetItem*> curSel;
    curSel.reserve(selList.size() * 2);
    for (QTreeWidgetItem* it : selList)
        curSel.insert(it);

    // Compute delta vs previous
    QSet<QTreeWidgetItem*> removed = _prevSelection;
    for (QTreeWidgetItem* it : curSel)
        removed.remove(it);

    QSet<QTreeWidgetItem*> added = curSel;
    for (QTreeWidgetItem* it : _prevSelection)
        added.remove(it);

    if (added.isEmpty() && removed.isEmpty())
    {
        emit selectionUpdated();
        return;
    }

    _updatingTree = true;

    auto applySubtreeSelect = [this](QTreeWidgetItem* root, bool select) {
        if (!root) return;

        // Leaf has no subtree, skip propagation
        if (root->data(0, IsLeafRole).toBool())
            return;

        QList<QTreeWidgetItem*> subtree;
        subtree.reserve(32);
        collectSubtreeItems(root, subtree); // your helper (root + descendants)

        for (QTreeWidgetItem* it : subtree)
            it->setSelected(select);
        };

    // Key ordering: removed first, then added (prevents the “2-level glitch”)
    for (QTreeWidgetItem* it : removed)
        applySubtreeSelect(it, false);

    for (QTreeWidgetItem* it : added)
        applySubtreeSelect(it, true);

    
    // Collect candidate parents affected by this change
    QSet<QTreeWidgetItem*> parentsToRefresh;
    parentsToRefresh.reserve((added.size() + removed.size()) * 2);

    auto addParentChainStart = [&](QTreeWidgetItem* it) {
        if (!it) return;
        QTreeWidgetItem* p = it->parent();
        if (p) parentsToRefresh.insert(p);
        };

    for (QTreeWidgetItem* it : added)   addParentChainStart(it);
    for (QTreeWidgetItem* it : removed) addParentChainStart(it);

    // Enforce: parent selected iff all direct children selected
    for (QTreeWidgetItem* p : parentsToRefresh)
        refreshParentSelectionUpward(p);

    _updatingTree = false;

    // Refresh prev selection snapshot AFTER propagation
    _prevSelection.clear();
    for (QTreeWidgetItem* it : selectedItems())
        _prevSelection.insert(it);

    // Now notify viewer
    emit selectionUpdated();
}


// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

bool SceneTreeWidget::nodeHasMeshes(const SceneNode* node) const
{
    if (!node->meshUuids.isEmpty()) return true;
    for (const SceneNode* child : node->children)
        if (nodeHasMeshes(child)) return true;
    return false;
}

void SceneTreeWidget::buildSubtree(QTreeWidgetItem* parentItem,
                                   const SceneNode* node)
{
    // Skip nodes whose entire subtree has been emptied (all meshes deleted).
    // The SceneGraph keeps these nodes alive for undo support; we simply
    // omit them from the view.  They reappear when structureChanged() fires
    // after an undo that restores a mesh into them.
    if (!nodeHasMeshes(node)) return;

    // -----------------------------------------------------------------------
    // Leaf optimisation: if this node has NO child SceneNodes it is a pure
    // mesh-holder — creating a 📦 wrapper around its mesh leaves adds no
    // structural information and produces the "every mesh has a placeholder
    // parent" clutter.  Add the mesh leaves directly to the parent instead.
    // -----------------------------------------------------------------------
    if (node->children.isEmpty())
    {
        for (const QUuid& uuid : node->meshUuids)
            parentItem->addChild(makeMeshLeaf(uuid));
        return;
    }

    // Node has child nodes → create an assembly item to group them.
    QTreeWidgetItem* item = makeAssemblyItem(node);
    parentItem->addChild(item);

    // Direct mesh leaves that sit on this assembly node itself
    for (const QUuid& uuid : node->meshUuids)
        item->addChild(makeMeshLeaf(uuid));

    // Recurse into child nodes (each skips empty subtrees internally)
    for (SceneNode* child : node->children)
        buildSubtree(item, child);

    item->setExpanded(true);
}

void SceneTreeWidget::enqueueRebuildTasks(QTreeWidgetItem* parentItem,
                                          const SceneNode* node,
                                          bool forceItem)
{
    if (!node || !nodeHasMeshes(node))
        return;
    _pendingBuildTasks.push_back({ parentItem, node, forceItem });
}

void SceneTreeWidget::processRebuildBatch()
{
    if (!_rebuildInProgress)
        return;

    // Use a time budget instead of a fixed item count so that large
    // assemblies (with many children / setExpanded) never block the
    // main-thread event loop for longer than ~8 ms.
    QElapsedTimer budget;
    budget.start();

    while (!_pendingBuildTasks.isEmpty() && budget.elapsed() < 8)
    {
        const BuildTask task = _pendingBuildTasks.takeFirst();
        const SceneNode* node = task.node;
        if (!node || !nodeHasMeshes(node))
            continue;

        QTreeWidgetItem* attachParent = task.parentItem;

        if (!task.forceItem && node->children.isEmpty())
        {
            if (!attachParent)
                continue;
            for (const QUuid& uuid : node->meshUuids)
                attachParent->addChild(makeMeshLeaf(uuid));
            continue;
        }

        QTreeWidgetItem* item = makeAssemblyItem(node);
        if (attachParent)
            attachParent->addChild(item);
        else
            addTopLevelItem(item);

        for (const QUuid& uuid : node->meshUuids)
            item->addChild(makeMeshLeaf(uuid));

        item->setExpanded(true);

        for (SceneNode* child : node->children)
            enqueueRebuildTasks(item, child, false);
    }

    viewport()->update();

    if (_pendingBuildTasks.isEmpty())
        finalizeRebuild();
    else
        _rebuildTimer->start(10);
}

void SceneTreeWidget::finalizeRebuild()
{
    if (_pendingVisibleUuids.isEmpty())
    {
        for (auto it = _uuidToLeaf.constBegin(); it != _uuidToLeaf.constEnd(); ++it)
            _pendingVisibleUuids.insert(it.key());
    }

    for (auto it = _uuidToLeaf.begin(); it != _uuidToLeaf.end(); ++it)
    {
        const bool isNew = !_pendingOldUuids.contains(it.key());
        const bool visible = isNew || _pendingVisibleUuids.contains(it.key());
        it.value()->setCheckState(0, visible ? Qt::Checked : Qt::Unchecked);
        updateItemIcon(it.value());
    }
    refreshAllAssemblyStates();

    for (const QUuid& uuid : std::as_const(_pendingSelectedUuids))
    {
        auto it = _uuidToLeaf.find(uuid);
        if (it != _uuidToLeaf.end())
            it.value()->setSelected(true);
    }

    for (int i = 0; i < topLevelItemCount(); ++i)
        refreshSelectionClosureBottomUp(topLevelItem(i));

    _prevSelection.clear();
    for (QTreeWidgetItem* it : selectedItems())
        _prevSelection.insert(it);

    _pendingBuildTasks.clear();
    _pendingVisibleUuids.clear();
    _pendingSelectedUuids.clear();
    _pendingOldUuids.clear();

    blockSignals(false);
    _updatingTree = false;
    _rebuildInProgress = false;
    viewport()->update();

	// Center the first selected item for better visibility after a rebuild
    scrollFirstSelectedToCenter();

    // Re-apply cut-mark styling if a Cut is pending.
    applyCutMarkStyling();

    emit rebuildComplete();
}

QTreeWidgetItem* SceneTreeWidget::makeMeshLeaf(const QUuid& uuid)
{
    QString name;
    if (_viewportWidget)
    {
        SceneMesh* mesh = _viewportWidget->getMeshByUuid(uuid);
        if (mesh) name = mesh->getName();
    }
    if (name.isEmpty())
        name = uuid.toString(QUuid::WithoutBraces).left(8);

    auto* item = new QTreeWidgetItem();
    item->setText(0, name);
    item->setIcon(0, treeIcons().meshNormal);
    item->setFlags(item->flags()
                 | Qt::ItemIsUserCheckable
                 | Qt::ItemIsEditable
                 | Qt::ItemIsSelectable);
    item->setCheckState(0, Qt::Checked);
    item->setData(0, MeshUuidRole, uuid);
    item->setData(0, IsLeafRole,   true);
    item->setData(0, PureNameRole, name);

    _uuidToLeaf[uuid] = item;
    return item;
}

QTreeWidgetItem* SceneTreeWidget::makeAssemblyItem(const SceneNode* node)
{
    auto* item = new QTreeWidgetItem();
    item->setText(0, node->name);
    item->setIcon(0, node->isSynthetic ? treeIcons().fileNormal
                                       : treeIcons().assemblyNormal);
      item->setFlags((item->flags()
                    | Qt::ItemIsUserCheckable
                    | Qt::ItemIsSelectable)
                    & ~Qt::ItemIsEditable);
      item->setCheckState(0, Qt::Checked);
      item->setData(0, IsLeafRole,      false);
      item->setData(0, IsSyntheticRole, node->isSynthetic);
      item->setData(0, NodeUuidRole,    node->nodeUuid);
      return item;
}

void SceneTreeWidget::propagateCheckDown(QTreeWidgetItem* item,
                                         Qt::CheckState   state)
{
    for (int i = 0; i < item->childCount(); ++i)
    {
        QTreeWidgetItem* child = item->child(i);
        child->setCheckState(0, state);
        updateItemIcon(child);
        if (child->childCount() > 0)
            propagateCheckDown(child, state);
    }
}

void SceneTreeWidget::refreshCheckUpward(QTreeWidgetItem* item)
{
    if (!item) return;

    item->setCheckState(0, aggregateChildCheckState(item));
    updateItemIcon(item);
    refreshCheckUpward(item->parent());
}

// Refreshes every assembly node's check-state and icon bottom-up.
// Use this after bulk leaf-state changes where refreshCheckUpward(topLevel)
// only reaches the root, skipping intermediate assembly nodes.
void SceneTreeWidget::refreshAllAssemblyStates()
{
    for (int i = 0; i < topLevelItemCount(); ++i)
        refreshAssemblyBottomUp(topLevelItem(i));
}

void SceneTreeWidget::refreshAssemblyBottomUp(QTreeWidgetItem* item)
{
    if (!item || item->data(0, IsLeafRole).toBool()) return;
    for (int i = 0; i < item->childCount(); ++i)
        refreshAssemblyBottomUp(item->child(i));

    item->setCheckState(0, aggregateChildCheckState(item));
    updateItemIcon(item);
}

Qt::CheckState SceneTreeWidget::aggregateChildCheckState(QTreeWidgetItem* item) const
{
    if (!item || item->childCount() == 0)
        return Qt::Unchecked;

    int checked = 0;
    int unchecked = 0;
    int partial = 0;

    for (int i = 0; i < item->childCount(); ++i)
    {
        switch (item->child(i)->checkState(0))
        {
        case Qt::Checked:
            ++checked;
            break;
        case Qt::Unchecked:
            ++unchecked;
            break;
        case Qt::PartiallyChecked:
            ++partial;
            break;
        }
    }

    if (partial > 0)
        return Qt::PartiallyChecked;
    if (checked > 0 && unchecked > 0)
        return Qt::PartiallyChecked;
    if (checked > 0)
        return Qt::Checked;
    return Qt::Unchecked;
}

void SceneTreeWidget::collectLeaves(QTreeWidgetItem*         root,
                                    QList<QTreeWidgetItem*>& out) const
{
    if (!root) return;
    if (root->data(0, IsLeafRole).toBool())
    {
        out.append(root);
        return;
    }
    for (int i = 0; i < root->childCount(); ++i)
        collectLeaves(root->child(i), out);
}

void SceneTreeWidget::collectAllLeaves(QList<QTreeWidgetItem*>& out) const
{
    for (int i = 0; i < topLevelItemCount(); ++i)
        collectLeaves(topLevelItem(i), out);
}

void SceneTreeWidget::collectExpandedItems(QTreeWidgetItem* subtree,
                                           QList<QTreeWidgetItem*>& out) const
{
    if (!subtree)
        return;

    if (subtree->childCount() > 0 && subtree->isExpanded())
        out.append(subtree);

    for (int i = 0; i < subtree->childCount(); ++i)
        collectExpandedItems(subtree->child(i), out);
}

void SceneTreeWidget::collectSubtreeItems(QTreeWidgetItem* root,
    QList<QTreeWidgetItem*>& out) const
{
    if (!root) return;
    out.append(root);
    for (int i = 0; i < root->childCount(); ++i)
        collectSubtreeItems(root->child(i), out);
}

void SceneTreeWidget::updateMeshName(const QUuid& uuid, const QString& name)
{
    auto it = _uuidToLeaf.find(uuid);
    if (it == _uuidToLeaf.end()) return;

    QTreeWidgetItem* item = it.value();

    // Block signals so the text change doesn't fire itemChanged and
    // trigger the rename or visibility machinery.
    _updatingTree = true;
    blockSignals(true);

    item->setText(0, name);
    item->setData(0, PureNameRole, name);

    blockSignals(false);
    _updatingTree = false;
}

int SceneTreeWidget::levenshteinDistance(const QString& s1,
                                          const QString& s2) const
{
    const int len1 = s1.size(), len2 = s2.size();
    QVector<QVector<int>> d(len1 + 1, QVector<int>(len2 + 1));

    for (int i = 0; i <= len1; ++i) d[i][0] = i;
    for (int j = 0; j <= len2; ++j) d[0][j] = j;

    for (int i = 1; i <= len1; ++i)
        for (int j = 1; j <= len2; ++j)
        {
            int cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
            d[i][j] = std::min({d[i-1][j] + 1,
                                d[i][j-1] + 1,
                                d[i-1][j-1] + cost});
        }

    return d[len1][len2];
}

QString SceneTreeWidget::itemSearchText(QTreeWidgetItem* item) const
{
    if (!item) return {};

    if (item->data(0, IsLeafRole).toBool())
        return item->data(0, PureNameRole).toString().toLower();

    return item->text(0).toLower();
}

QString SceneTreeWidget::itemPathSearchText(QTreeWidgetItem* item) const
{
    if (!item) return {};

    QStringList parts;
    for (QTreeWidgetItem* cur = item; cur; cur = cur->parent())
    {
        const QString part = itemSearchText(cur);
        if (!part.isEmpty())
            parts.prepend(part);
    }
    return parts.join(' ');
}

QStringList SceneTreeWidget::searchTerms(const QString& text) const
{
    QString normalized = text;

    // Split common separators and camelCase boundaries into searchable terms.
    normalized.replace(QRegularExpression("([a-z0-9])([A-Z])"), "\\1 \\2");
    normalized = normalized.toLower();
    normalized.replace(QRegularExpression("[^a-z0-9]+"), " ");

    return normalized.split(' ', Qt::SkipEmptyParts);
}

int SceneTreeWidget::textMatchRank(QTreeWidgetItem* item, const QString& lowerFilter) const
{
    if (!item || lowerFilter.isEmpty())
        return 0;

    const QString text = itemSearchText(item);
    const QString pathText = itemPathSearchText(item);
    if (text.isEmpty() && pathText.isEmpty())
        return 0;

    const QStringList queryTerms = searchTerms(lowerFilter);
    const QStringList terms = searchTerms(text);
    const QStringList pathTerms = searchTerms(pathText);
    const int leafBonus = item->data(0, IsLeafRole).toBool() ? 4 : 0;
    if (queryTerms.isEmpty())
        return 0;

    auto allTermsMatch = [](const QStringList& haystackTerms,
                            const QStringList& needles,
                            auto predicate) -> bool
    {
        for (const QString& needle : needles)
        {
            bool found = false;
            for (const QString& hay : haystackTerms)
            {
                if (predicate(hay, needle))
                {
                    found = true;
                    break;
                }
            }
            if (!found)
                return false;
        }
        return true;
    };

    if (text == lowerFilter)
        return 80 + leafBonus;
    if (pathText == lowerFilter)
        return 70 + leafBonus;

    if (allTermsMatch(terms, queryTerms,
                      [](const QString& hay, const QString& needle) { return hay == needle; }))
        return 72 + leafBonus;
    if (allTermsMatch(pathTerms, queryTerms,
                      [](const QString& hay, const QString& needle) { return hay == needle; }))
        return 62 + leafBonus;
    if (allTermsMatch(terms, queryTerms,
                      [](const QString& hay, const QString& needle) { return hay.startsWith(needle); }))
        return 54 + leafBonus;
    if (allTermsMatch(pathTerms, queryTerms,
                      [](const QString& hay, const QString& needle) { return hay.startsWith(needle); }))
        return 44 + leafBonus;
    if (allTermsMatch(terms, queryTerms,
                      [](const QString& hay, const QString& needle) { return hay.contains(needle); }))
        return 36 + leafBonus;
    if (allTermsMatch(pathTerms, queryTerms,
                      [](const QString& hay, const QString& needle) { return hay.contains(needle); }))
        return 26 + leafBonus;

    if (text.contains(lowerFilter))
        return 24 + leafBonus;
    if (pathText.contains(lowerFilter))
        return 14 + leafBonus;

    for (const QString& term : terms)
        if (term.startsWith(lowerFilter))
            return 54 + leafBonus;

    return 0;
}

int SceneTreeWidget::fuzzyThreshold(const QString& lowerFilter) const
{
    const int len = searchTerms(lowerFilter).join("").size();
    if (len <= 2) return 0;
    if (len <= 4) return 1;
    if (len <= 7) return 2;
    if (len <= 11) return 3;
    return 4;
}

void SceneTreeWidget::showSubtree(QTreeWidgetItem* item)
{
    if (!item) return;

    item->setHidden(false);
    item->setExpanded(true);
    for (int i = 0; i < item->childCount(); ++i)
        showSubtree(item->child(i));
}

void SceneTreeWidget::selectSearchMatch(QTreeWidgetItem* item)
{
    if (!item) return;

    if (item->data(0, IsLeafRole).toBool())
    {
        item->setSelected(true);
        return;
    }

    QList<QTreeWidgetItem*> leaves;
    collectLeaves(item, leaves);
    for (QTreeWidgetItem* leaf : leaves)
        leaf->setSelected(true);
}

bool SceneTreeWidget::applySubstringFilter(QTreeWidgetItem* item,
                                           const QString& lowerFilter,
                                           bool ancestorMatched,
                                           bool& anyMatch,
                                           int& bestRank)
{
    if (!item) return false;

    const int ownRank = textMatchRank(item, lowerFilter);
    const bool ownMatch = ownRank > 0;
    bool descendantMatch = false;

    for (int i = 0; i < item->childCount(); ++i)
    {
        if (applySubstringFilter(item->child(i),
                                 lowerFilter,
                                 ancestorMatched || ownMatch,
                                 anyMatch,
                                 bestRank))
        {
            descendantMatch = true;
        }
    }

    const bool visible = ancestorMatched || ownMatch || descendantMatch;
    item->setHidden(!visible);

    if (!visible)
        return false;

    if (item->childCount() > 0 && (ownMatch || descendantMatch))
        item->setExpanded(true);

    if (ownMatch)
    {
        anyMatch = true;
        bestRank = std::max(bestRank, ownRank);
        if (!item->data(0, IsLeafRole).toBool())
            showSubtree(item);
    }

    return ownMatch || descendantMatch;
}

void SceneTreeWidget::selectMatchesAtRank(QTreeWidgetItem* item,
                                          const QString& lowerFilter,
                                          int bestRank)
{
    if (!item) return;

    if (!item->isHidden() && textMatchRank(item, lowerFilter) == bestRank)
        selectSearchMatch(item);

    for (int i = 0; i < item->childCount(); ++i)
        selectMatchesAtRank(item->child(i), lowerFilter, bestRank);
}

QTreeWidgetItem* SceneTreeWidget::findBestFuzzyMatch(QTreeWidgetItem* item,
                                                     const QString& lowerFilter,
                                                     int& bestScore) const
{
    if (!item) return nullptr;

    QTreeWidgetItem* bestItem = nullptr;
    const QString text = itemSearchText(item);
    const QString pathText = itemPathSearchText(item);
    if (!text.isEmpty() || !pathText.isEmpty())
    {
        int score = std::numeric_limits<int>::max();
        if (!text.isEmpty())
            score = std::min(score, levenshteinDistance(lowerFilter, text));
        if (!pathText.isEmpty())
            score = std::min(score, levenshteinDistance(lowerFilter, pathText));
        for (const QString& term : searchTerms(text))
            score = std::min(score, levenshteinDistance(lowerFilter, term));
        for (const QString& term : searchTerms(pathText))
            score = std::min(score, levenshteinDistance(lowerFilter, term));
        if (score < bestScore)
        {
            bestScore = score;
            bestItem = item;
        }
    }

    for (int i = 0; i < item->childCount(); ++i)
    {
        QTreeWidgetItem* childBest =
            findBestFuzzyMatch(item->child(i), lowerFilter, bestScore);
        if (childBest)
            bestItem = childBest;
    }

    return bestItem;
}

void SceneTreeWidget::expandOneLevel(QTreeWidgetItem* item)
{
    if (!item) return;
    item->setExpanded(true);
    // Collapse every direct child so their subtrees stay hidden.
    // Without this, children that were previously expanded would show their
    // grandchildren too, making "Expand" indistinguishable from "Expand All".
    for (int i = 0; i < item->childCount(); ++i)
        item->child(i)->setExpanded(false);
}

void SceneTreeWidget::expandSubtree(QTreeWidgetItem* item)
{
    if (!item) return;
    expandItem(item);
    for (int i = 0; i < item->childCount(); ++i)
        expandSubtree(item->child(i));
}

void SceneTreeWidget::collapseOneLevel(QTreeWidgetItem* item)
{
    // Collapse each direct child so its contents are hidden, but leave
    // 'item' itself expanded — the children stay visible and can be
    // individually re-opened.  Deeper levels are untouched.
    if (!item) return;
    for (int i = 0; i < item->childCount(); ++i)
        collapseItem(item->child(i));
    // item stays expanded
}

void SceneTreeWidget::collapseAllBelow(QTreeWidgetItem* item)
{
    // Deep-collapse the whole subtree rooted at 'item'. This matches the
    // context-menu expectation that "Collapse All Children" hides even direct
    // mesh leaves immediately under the clicked assembly node.
    if (!item) return;
    collapseSubtreeHelper(item);
}

// Internal recursive helper: collapse 'item' and all its descendants.
void SceneTreeWidget::collapseSubtreeHelper(QTreeWidgetItem* item)
{
    if (!item) return;
    for (int i = 0; i < item->childCount(); ++i)
        collapseSubtreeHelper(item->child(i));
    // Use setExpanded(false) directly — collapseItem() can silently no-op
    // on items that Qt's view hasn't yet materialised (first invocation).
    item->setExpanded(false);
}

bool SceneTreeWidget::allDirectChildrenSelected(QTreeWidgetItem* parent) const
{
    if (!parent) return false;
    const int n = parent->childCount();
    if (n == 0) return false;

    for (int i = 0; i < n; ++i)
    {
        QTreeWidgetItem* c = parent->child(i);
        if (!c || !c->isSelected())
            return false;
    }
    return true;
}


void SceneTreeWidget::refreshParentSelectionUpward(QTreeWidgetItem* startParent)
{
    for (QTreeWidgetItem* p = startParent; p; p = p->parent())
    {
        const bool shouldSelect = allDirectChildrenSelected(p);

        if (p->isSelected() == shouldSelect)
            continue;

        p->setSelected(shouldSelect);
    }
}

void SceneTreeWidget::refreshSelectionClosureBottomUp(QTreeWidgetItem* item)
{
    if (!item) return;

    // Recurse first (post-order)
    for (int i = 0; i < item->childCount(); ++i)
        refreshSelectionClosureBottomUp(item->child(i));

    // Leaves: no upward meaning; assemblies/files: enforce Option A
    if (!item->data(0, IsLeafRole).toBool())
    {
        const bool shouldSelect = allDirectChildrenSelected(item);
        if (item->isSelected() != shouldSelect)
            item->setSelected(shouldSelect);
    }
}


void SceneTreeWidget::updateItemIcon(QTreeWidgetItem* item)
{
    if (!item) return;
    const Qt::CheckState cs = item->checkState(0);
    if (item->data(0, IsLeafRole).toBool())
    {
        item->setIcon(0, cs == Qt::Checked
                      ? treeIcons().meshNormal
                      : treeIcons().meshGrey);
    }
    else
    {
        const bool synthetic = item->data(0, IsSyntheticRole).toBool();
        const bool allHidden = (cs == Qt::Unchecked);
        if (synthetic)
            item->setIcon(0, allHidden ? treeIcons().fileGrey     : treeIcons().fileNormal);
        else
            item->setIcon(0, allHidden ? treeIcons().assemblyGrey : treeIcons().assemblyNormal);
    }
}

const SceneNode* SceneTreeWidget::nodeAt(const QPoint& localPos) const
{
    QTreeWidgetItem* item = itemAt(localPos);
    if (!item || item->data(0, IsLeafRole).toBool())
        return nullptr;

    const QUuid nodeUuid = item->data(0, NodeUuidRole).value<QUuid>();
    if (nodeUuid.isNull() || !_sceneGraph)
        return nullptr;

    return _sceneGraph->findNodeByUuid(nodeUuid);
}

void SceneTreeWidget::highlightSingleItemAt(const QPoint& localPos)
{
    QTreeWidgetItem* item = itemAt(localPos);
    if (!item) return;

    QSignalBlocker blocker(this);
    clearSelection();
    item->setSelected(true);
}

QList<const SceneNode*> SceneTreeWidget::selectedAssemblyNodes() const
{
    QList<const SceneNode*> result;
    if (!_sceneGraph)
        return result;

    for (QTreeWidgetItem* item : selectedItems())
    {
        if (item->data(0, IsLeafRole).toBool())
            continue;

        const QUuid nodeUuid = item->data(0, NodeUuidRole).value<QUuid>();
        if (nodeUuid.isNull())
            continue;

        if (const SceneNode* node = _sceneGraph->findNodeByUuid(nodeUuid))
            result.append(node);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Cut-mark styling
// ---------------------------------------------------------------------------

void SceneTreeWidget::markAsCut(const QSet<QUuid>& meshUuids,
                                const QSet<QUuid>& nodeUuids)
{
    _cutMeshUuids = meshUuids;
    _cutNodeUuids = nodeUuids;
    applyCutMarkStyling();
}

void SceneTreeWidget::clearCutMarks()
{
    _cutMeshUuids.clear();
    _cutNodeUuids.clear();

    // Walk every item and reset the foreground role to the default.
    std::function<void(QTreeWidgetItem*)> resetItem = [&](QTreeWidgetItem* item)
    {
        item->setData(0, Qt::ForegroundRole, QVariant());
        for (int i = 0; i < item->childCount(); ++i)
            resetItem(item->child(i));
    };

    for (int i = 0; i < topLevelItemCount(); ++i)
        resetItem(topLevelItem(i));
}

void SceneTreeWidget::applyCutMarkStyling()
{
    if (_cutMeshUuids.isEmpty() && _cutNodeUuids.isEmpty())
        return;

    // Walk the entire tree.  When a cut-marked assembly is found, apply the
    // style to it and all its descendants (so expanded children look grayed).
    // When a cut-marked mesh leaf is found, style only that leaf.
    std::function<void(QTreeWidgetItem*)> walk = [&](QTreeWidgetItem* item)
    {
        if (item->data(0, IsLeafRole).toBool())
        {
            const QUuid uuid = item->data(0, MeshUuidRole).value<QUuid>();
            if (_cutMeshUuids.contains(uuid))
                applyCutStyleRecursive(item, true);
        }
        else
        {
            const QUuid nodeUuid = item->data(0, NodeUuidRole).value<QUuid>();
            if (_cutNodeUuids.contains(nodeUuid))
            {
                applyCutStyleRecursive(item, true);
                return; // descendants handled by the recursive call
            }
        }
        for (int i = 0; i < item->childCount(); ++i)
            walk(item->child(i));
    };

    for (int i = 0; i < topLevelItemCount(); ++i)
        walk(topLevelItem(i));
}

void SceneTreeWidget::applyCutStyleRecursive(QTreeWidgetItem* item, bool isCut)
{
    if (!item) return;
    static const QBrush kCutBrush(QColor(128, 128, 128));
    if (isCut)
        item->setForeground(0, kCutBrush);
    else
        item->setData(0, Qt::ForegroundRole, QVariant());

    for (int i = 0; i < item->childCount(); ++i)
        applyCutStyleRecursive(item->child(i), isCut);
}
