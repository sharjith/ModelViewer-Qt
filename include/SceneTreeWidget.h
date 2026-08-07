#pragma once

#include <QHash>
#include <QList>
#include <QPalette>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QTreeWidget>
#include <QUuid>
#include <vector>

#include "SceneNode.h"

class SceneGraph;
class ViewportWidget;

// ---------------------------------------------------------------------------
// SceneTreeWidget
//
// A QTreeWidget that mirrors the SceneGraph hierarchy.  Replaces the old
// flat ModelObjectList (QListWidget).
//
// Node types shown (icons from Qt resources):
//   document-root.png  Synthetic file-level node (one per imported file)
//   assembly.png       Assembly node (has child SceneNodes)
//   mesh.png           Mesh leaf (one per SceneMesh UUID in a SceneNode)
//
// Assembly / file items carry tri-state check boxes that control all leaves
// beneath them.  Clicking an assembly item selects all mesh leaves below it.
// Empty assembly nodes (all children deleted) are hidden from the tree but
// kept alive in SceneGraph so undo can restore them without reconstruction.
//
// Thread safety: all public methods must be called from the UI thread.
// ---------------------------------------------------------------------------
class SceneTreeWidget : public QTreeWidget
{
    Q_OBJECT

public:
    // Data roles stored on QTreeWidgetItem instances
    enum ItemDataRole
    {
        MeshUuidRole    = Qt::UserRole,       // QUuid  — valid only on mesh leaf items
        IsLeafRole      = Qt::UserRole + 1,   // bool   — true if this is a mesh leaf
        PureNameRole    = Qt::UserRole + 2,   // QString— plain mesh name for the editor
        IsSyntheticRole = Qt::UserRole + 3,   // bool   — true on file-level (synthetic) nodes
        NodeUuidRole    = Qt::UserRole + 4,   // QUuid  — nodeUuid for assembly/file items
    };

    explicit SceneTreeWidget(QWidget* parent = nullptr);

    void setSceneGraph(SceneGraph* sg);
    void setViewportWidget(ViewportWidget* viewportWidget);

    // -----------------------------------------------------------------------
    // Selection
    // -----------------------------------------------------------------------

    /** Returns UUIDs of all currently selected mesh-leaf items. */
    QList<QUuid> selectedMeshUuids() const;

    /** True if at least one mesh leaf is selected. */
    bool hasMeshSelection() const;

    /** Total number of mesh leaf items currently in the tree. */
    int meshCount() const;

    /**
     * Set selection from a set of mesh-store indices (converted via ViewportWidget).
     * Does NOT emit selectionUpdated().
     */
    void setSelectionByIndices(const QSet<int>& indices);

    /**
     * Set selection from a set of mesh UUIDs.
     * Does NOT emit selectionUpdated().
     */
    void setSelectionByUuids(const QSet<QUuid>& uuids);

    /** Returns mesh-store indices for all selected mesh leaves. */
    std::vector<int> getSelectedIndices() const;

    /** UUID of the current (last-clicked) item, or null if not a mesh leaf. */
    QUuid currentMeshUuid() const;

    /** Clear the selection without emitting selectionUpdated(). */
    void clearMeshSelection();

    // -----------------------------------------------------------------------
    // Visibility (check state)
    // -----------------------------------------------------------------------

    /**
     * Update check states from the set of currently-visible mesh UUIDs.
     * Does NOT emit meshVisibilityChanged().
     */
    void setVisibilityByUuids(const QSet<QUuid>& visibleUuids);

    /**
     * Update only the specified mesh leaves from the visible set, then refresh
     * only the affected ancestor chain(s). Used for small selective edits.
     * Does NOT emit meshVisibilityChanged().
     */
    void setVisibilityDelta(const QSet<QUuid>& changedUuids,
                            const QSet<QUuid>& visibleUuids);

    /** Returns UUIDs of all checked (visible) mesh leaves. */
    QSet<QUuid> getVisibleUuids() const;

    /** Returns mesh-store indices of all checked (visible) mesh leaves. */
    std::vector<int> getVisibleIndices() const;

    /** Count of checked (visible) mesh leaves. */
    int visibleMeshCount() const;

    // -----------------------------------------------------------------------
    // Filter / search
    // -----------------------------------------------------------------------

    /**
     * Filter the tree by item text (assembly + mesh names, with fuzzy fallback).
     * Matching branches stay visible; if an assembly matches, its descendant
     * mesh leaves become the effective selection. An empty filter restores all
     * items.
     * Emits selectionUpdated() after updating the selection.
     */
    void filterItems(const QString& filter);

    // -----------------------------------------------------------------------
    // Tree expand / collapse helpers (used by context menu)
    // -----------------------------------------------------------------------

    /** Expand item one level: shows direct children; grandchildren untouched. */
    // (use the inherited QTreeWidget::expandItem() directly for this)

    /**
     * Expand item one level strictly: shows direct children but ensures
     * those children are collapsed so their subtrees stay hidden.
     * (Plain expandItem() shows previously-expanded grandchildren too.)
     */
    void expandOneLevel(QTreeWidgetItem* item);

    /** Expand item AND all of its descendants recursively. */
    void expandSubtree(QTreeWidgetItem* item);

    /**
     * Collapse one level: collapse each DIRECT CHILD of item so their
     * contents are hidden, but item itself stays expanded (children remain
     * visible and can be individually re-opened).  Deeper levels untouched.
     */
    void collapseOneLevel(QTreeWidgetItem* item);

    /**
     * Collapse all below: deeply collapse every descendant subtree so that
     * re-opening any child shows it in a fully-collapsed state.  Item itself
     * stays expanded so its direct children remain visible.
     */
    void collapseAllBelow(QTreeWidgetItem* item);

    // -----------------------------------------------------------------------
    // Misc
    // -----------------------------------------------------------------------

    /** Map a local widget position to global screen coordinates. */
    QPoint mapMenuToGlobal(const QPoint& localPos) const;
    bool isAssemblyAt(const QPoint& localPos) const;
    bool hasChildrenAt(const QPoint& localPos) const;
    void ensureAssemblySelectionAt(const QPoint& localPos);

    // Return the SceneNode* for the item at localPos, or nullptr if the item
    // is a mesh leaf or there is no item at that position.
    const SceneNode* nodeAt(const QPoint& localPos) const;

    // Return the SceneNode* for each selected non-leaf item (assembly / file).
    // Top-level only — does not deduplicate against selected ancestors.
    QList<const SceneNode*> selectedAssemblyNodes() const;

    // Clear the current selection and select only the item at localPos.
    // Blocks selectionUpdated so no downstream handlers fire.
    // Used by the context menu to give single-item visual feedback.
    void highlightSingleItemAt(const QPoint& localPos);

    void expandOneLevelAt(const QPoint& localPos);
    void expandSubtreeAt(const QPoint& localPos);
    void collapseAllBelowAt(const QPoint& localPos);
    void scrollFirstSelectedToCenter();
    void setDetachedOverlayMode(bool enabled);

    // -----------------------------------------------------------------------
    // Cut-mark styling
    // -----------------------------------------------------------------------

    /**
     * Gray out the given mesh leaves and assembly nodes to signal a pending
     * Cut.  Styling propagates down to all descendants of marked assemblies.
     * Persists across tree rebuilds.
     */
    void markAsCut(const QSet<QUuid>& meshUuids, const QSet<QUuid>& nodeUuids);

    /** Remove all cut-mark styling and clear the stored sets. */
    void clearCutMarks();

public slots:
    /**
     * Fully rebuild the tree from the current SceneGraph state.
     * Automatically called when SceneGraph::structureChanged() fires.
     * Preserves existing visibility and selection state where possible.
     * Assembly nodes whose entire subtree has been deleted are hidden.
     */
    void rebuild();

    /**
     * Update the display name of a single mesh leaf without triggering a
     * full rebuild.  Used by RenameMeshCommand redo/undo to apply or
     * revert a rename efficiently.
     */
    void updateMeshName(const QUuid& uuid, const QString& name);

signals:
    /** Emitted when the user changes the selection (not programmatic). */
    void selectionUpdated();

    /** Emitted when any leaf check-state (visibility) changes. */
    void meshVisibilityChanged();

    /** Emitted when a mesh leaf is successfully renamed by the user. */
    void meshRenamed(const QUuid& uuid, const QString& newName);

    /** Emitted when the asynchronous tree rebuild has fully completed. */
    void rebuildComplete();

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    // Ignored (not scrolled) rather than accepted - the tree lives as a
    // transparent overlay glued to the viewport (see
    // ModelViewer::attachNavigationOverlay()), and an ignored wheel event
    // propagates up to the viewport underneath instead, so scrolling over
    // the tree still zooms the 3D view like scrolling anywhere else in it.
    void wheelEvent(QWheelEvent* event) override;

private slots:
    void onItemChanged(QTreeWidgetItem* item, int column);
    void onItemSelectionChanged();
    void processRebuildBatch();

private:
    struct BuildTask
    {
        QTreeWidgetItem* parentItem = nullptr;
        const SceneNode* node = nullptr;
        bool forceItem = false;
    };

    // Recursive tree builder — skips nodes with no mesh descendants
    void buildSubtree(QTreeWidgetItem* parentItem, const SceneNode* node);
    void enqueueRebuildTasks(QTreeWidgetItem* parentItem,
                             const SceneNode* node,
                             bool forceItem = false);
    void finalizeRebuild();

    // Returns true if node has at least one mesh UUID anywhere in its subtree
    bool nodeHasMeshes(const SceneNode* node) const;

    // Item factories
    QTreeWidgetItem* makeMeshLeaf(const QUuid& uuid);
    QTreeWidgetItem* makeAssemblyItem(const SceneNode* node);

    // Tri-state helpers
    void propagateCheckDown(QTreeWidgetItem* item, Qt::CheckState state);
    void refreshCheckUpward(QTreeWidgetItem* item);

    // Enumerate all leaf items (DFS)
    void collectLeaves(QTreeWidgetItem* subtree,
                       QList<QTreeWidgetItem*>& out) const;
    void collectAllLeaves(QList<QTreeWidgetItem*>& out) const;
    void collectExpandedItems(QTreeWidgetItem* subtree,
                              QList<QTreeWidgetItem*>& out) const;


    void collectSubtreeItems(QTreeWidgetItem* subtree,
        QList<QTreeWidgetItem*>& out) const;

    // Internal helper: collapse item and all its descendants
    void collapseSubtreeHelper(QTreeWidgetItem* item);

    bool allDirectChildrenSelected(QTreeWidgetItem* parent) const;
    void refreshParentSelectionUpward(QTreeWidgetItem* startParent);
    void refreshSelectionClosureBottomUp(QTreeWidgetItem* item);

    // Bulk assembly tristate + icon refresh (used after show/hide all)
    void refreshAllAssemblyStates();
    void refreshAssemblyBottomUp(QTreeWidgetItem* item);
    Qt::CheckState aggregateChildCheckState(QTreeWidgetItem* item) const;

    // Sync the item's icon to its current check-state
    void updateItemIcon(QTreeWidgetItem* item);

    // Levenshtein distance for fuzzy name matching
    int levenshteinDistance(const QString& s1, const QString& s2) const;

    // Search / filter helpers
    QString itemSearchText(QTreeWidgetItem* item) const;
    QString itemPathSearchText(QTreeWidgetItem* item) const;
    QStringList searchTerms(const QString& text) const;
    int textMatchRank(QTreeWidgetItem* item, const QString& lowerFilter) const;
    int fuzzyThreshold(const QString& lowerFilter) const;
    void showSubtree(QTreeWidgetItem* item);
    void selectSearchMatch(QTreeWidgetItem* item);
    void selectMatchesAtRank(QTreeWidgetItem* item,
                             const QString& lowerFilter,
                             int bestRank);
    bool applySubstringFilter(QTreeWidgetItem* item,
                              const QString& lowerFilter,
                              bool ancestorMatched,
                              bool& anyMatch,
                              int& bestRank);
    QTreeWidgetItem* findBestFuzzyMatch(QTreeWidgetItem* item,
                                        const QString& lowerFilter,
                                        int& bestScore) const;

    SceneGraph* _sceneGraph = nullptr;
    ViewportWidget*   _viewportWidget   = nullptr;

    // O(1) lookup from mesh UUID to its leaf QTreeWidgetItem
    QHash<QUuid, QTreeWidgetItem*> _uuidToLeaf;
    QTimer* _rebuildTimer = nullptr;
    QList<BuildTask> _pendingBuildTasks;
    QSet<QUuid> _pendingVisibleUuids;
    QSet<QUuid> _pendingSelectedUuids;
    QSet<QUuid> _pendingOldUuids;
    bool _rebuildInProgress = false;

    QPersistentModelIndex _pressIndex;
    Qt::KeyboardModifiers _pressMods = Qt::NoModifier;
    bool _pressWasSelected = false;
    Qt::CheckState _pressCheckState = Qt::Unchecked;
    bool _pressExpanded = false;
    bool _pressValid = false;
    QSet<QTreeWidgetItem*> _prevSelection;

    // Apply/clear cut-mark gray styling.  Called by markAsCut, clearCutMarks,
    // and finalizeRebuild (to re-apply marks after a tree rebuild).
    void applyCutMarkStyling();
    void applyCutStyleRecursive(QTreeWidgetItem* item, bool isCut);

    QSet<QUuid> _cutMeshUuids;  // mesh UUIDs currently marked as cut
    QSet<QUuid> _cutNodeUuids;  // assembly nodeUuids currently marked as cut

    bool _updatingTree = false; // suppress re-entrant signal handling
    bool _inRename     = false; // suppress itemChanged during delegate setModelData
    bool _detachedOverlayMode = false;
    bool _savedAutoFillBackground = false;
    bool _savedViewportAutoFillBackground = false;
    QPalette _savedTreePalette;
    QPalette _savedViewportPalette;
    QString _savedStyleSheet;
    QColor _detachedOverlayFillColor = QColor(255, 255, 255, 65);
};
