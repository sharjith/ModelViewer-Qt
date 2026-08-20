#pragma once


#include "SceneMeshRecord.h"
#include "MeshSurfaceAnchor.h"

#include <QObject>
#include <QPoint>
#include <QList>
#include <QMap>
#include <QVector3D>

class ViewportWidget;
class Camera;
class RenderableMesh;
// Hover highlighting mode enumeration
enum class HoverHighlightMode
{    
    RaycastOnly,   // Fast ray-casting preview
    Accurate,       // Full hybrid (expensive, but accurate with FBO rendering)
    Disabled      // No hover feedback
};

// Click selection mode enumeration
enum class SelectionMode
{
    RayOnly,
    ColorOnly,
    Hybrid // Try ray, fallback to color
};

/**
 * @class SelectionManager
 * @brief Manages all mesh selection operations and state
 *
 * This manager encapsulates all selection logic including:
 * - Click-based selection (single, sweep, multi-select)
 * - Hover-based selection preview
 * - Color picking via FBO rendering
 * - Ray-casting intersection detection
 *
 * Signals are emitted when selection state changes, allowing other
 * components to respond without tight coupling.
 */
class SelectionManager : public QObject
{
    Q_OBJECT

public:
    explicit SelectionManager(
        ViewportWidget* viewportWidget,
        Camera* primaryCamera,
        std::vector<SceneMeshRecord>& meshStore,
        const std::vector<int>& displayedObjectsIds,
        const std::vector<int>& hiddenObjectsIds,
        bool& visibleSwapped,
        QObject* parent = nullptr);

    ~SelectionManager();

    // Selection operations
    int clickSelect(const QPoint& pixel);
    int hoverSelect(const QPoint& pixel);
    QList<int> sweepSelect(const QPoint& p1, const QPoint& p2, bool addToSelection = false);
    void select(int id);
    void deselect(int id);
    void syncMeshSelectionVisualState();
    int processSelection(const QPoint& pixel);

    // Pick a precise surface anchor at the given pixel: the closest
    // ray-triangle hit across all visible meshes, with vertex-snapping
    // (nearest of the hit triangle's 3 vertices, if within snapPixelRadius
    // screen pixels). For Measurement/Annotation point picking - unlike
    // clickSelect()/hoverSelect(), this returns geometric detail (triangle
    // index + barycentric weights), not just a mesh id, and it does not
    // touch selection state. Returns an anchor with triangleIndex == -1
    // (isValid() == false) if nothing was hit.
    MeshSurfaceAnchor pickSurfaceAnchor(const QPoint& pixel, int snapPixelRadius = 8);

    // State queries
    QList<int> getSelectedIds() const { return _selectedMeshIds; }
    int getHoveredId() const { return _hoveredMeshId; }
    HoverHighlightMode getHoverMode() const { return _hoverHighlightMode; }
    SelectionMode getSelectionMode() const { return _selectionMode; }

    // State setters (for sync with ViewportWidget after sweep selection)
    void setSelectedIds(const QList<int>& selectedIds) {
        _selectedMeshIds = selectedIds;
        emit selectionChanged(_selectedMeshIds);
    }

    // Sync selection state without emitting signal (used by sweep selection to avoid feedback loops)
    void syncSelectedIds(const QList<int>& selectedIds) {
        _selectedMeshIds = selectedIds;
    }

    // FBO management (called by ViewportWidget)
    void initializeFBOResources();
    void cleanupFBOResources();
    void resizeFBOResources(int width, int height);

public slots:
    void setHoverHighlightMode(HoverHighlightMode mode);
    void setSelectionMode(SelectionMode mode);

signals:
    void selectionChanged(const QList<int>& selectedIds);
    void hoverChanged(int hoveredId);
    void hoverModeChanged(HoverHighlightMode mode);
    void selectionModeChanged(SelectionMode mode);

private:
    // Helper methods for ray-casting
    void getRayFromPixelCoords(const QPoint& pixel, QVector3D& rayPos, QVector3D& rayDir);
    void convertClickToRay(const QPoint& pixel, const QRect& viewport,
                          Camera* camera, QVector3D& rayPos, QVector3D& rayDir);

    // State members
    QList<int> _selectedMeshIds;           // Currently selected mesh IDs
    int _hoveredMeshId = -1;               // Currently hovered mesh (-1 = none)
    HoverHighlightMode _hoverHighlightMode = HoverHighlightMode::RaycastOnly;
    SelectionMode _selectionMode = SelectionMode::Hybrid;

    // FBO resources for color picking
    unsigned int _selectionFBO = 0;
    unsigned int _selectionRBO = 0;        // Color render buffer
    unsigned int _selectionDBO = 0;        // Depth render buffer

    // References to ViewportWidget data (don't own these)
    ViewportWidget* _viewportWidget;
    Camera* _primaryCamera;
    std::vector<SceneMeshRecord>& _meshStore;
    const std::vector<int>& _displayedObjectsIds;
    const std::vector<int>& _hiddenObjectsIds;
    bool& _visibleSwapped;

    // Cached viewport dimensions for FBO
    int _fboWidth = 0;
    int _fboHeight = 0;
};
