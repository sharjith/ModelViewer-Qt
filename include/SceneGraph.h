#pragma once

#include "SceneNode.h"
#include "PunctualLights.h"
#include "GltfAnimationData.h"
#include "GltfCameraData.h"
#include "GltfLightData.h"
#include "GltfVariantData.h"
#include "MeasurementData.h"

#include <QHash>
#include <QJsonArray>
#include <QMatrix4x4>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QDataStream>
#include <assimp/scene.h>

struct SceneGraphWorldTransforms
{
    QHash<QUuid, QMatrix4x4> nodeWorldByUuid;
    QHash<QUuid, QMatrix4x4> meshWorldByUuid;
};

// ---------------------------------------------------------------------------
// SceneGraph
//
// Owns and manages the complete scene node hierarchy.  It is the canonical
// data model for:
//   - the tree widget (reads the hierarchy, connects to structureChanged)
//   - the export path (calls reconstructAsScene() instead of walking
//     _globalScene, which may contain stale / deleted meshes)
//
// Lifetime: owned by ModelViewer.  Neither ViewportWidget nor any command class
// should hold or delete SceneGraph directly.
//
// Thread safety: all methods must be called from the main (UI) thread.
// ---------------------------------------------------------------------------
class SceneGraph : public QObject
{
    Q_OBJECT

public:
    explicit SceneGraph(QObject* parent = nullptr);
    ~SceneGraph();

    // -----------------------------------------------------------------------
    // Build
    // -----------------------------------------------------------------------

    // Append one imported file's hierarchy to the scene.
    //
    // scene              — the aiScene* returned by AssImpModelLoader::getScene()
    //                      BEFORE it is deep-copied and merged into _globalScene.
    // sourceFile         — absolute path of the file that was loaded.
    // meshUuidsInOrder   — UUIDs of every SceneMesh created from this file,
    //                      in the same DFS order that
    //                      AssImpModelLoader::processNode() visited them.
    //                      The cursor-based DFS inside buildSubtree() assigns
    //                      these UUIDs to the matching aiNodes automatically.
    // lights             — optional punctual lights from KHR_lights_punctual extension.
    // importCorrection   — autoOrient+autoScale matrix the loader applied to the Assimp root
    //                      node before the SceneGraph was built; stored on the synthetic
    //                      fileNode so exporters can factor it out.  Pass identity if none.
    // autoOrientApplied  — true if the loader applied a coordinate-system rotation.
    // autoScaleApplied   — true if the loader applied an auto-scale to normalise scene size.
    void appendFromScene(const aiScene*                   scene,
                         const QString&                   sourceFile,
                         const QList<QUuid>&              meshUuidsInOrder,
                         const aiMatrix4x4&               importCorrection = aiMatrix4x4(),
                         bool                             autoOrientApplied = false,
                         bool                             autoScaleApplied  = false);

    // Build a flat synthetic session node that owns all meshes directly.
    // Used as a fallback for native-session files that do not carry a full
    // hierarchy, or when loading legacy MVF files.
    void rebuildFlat(const QString& sessionName,
                     const QList<QUuid>& meshUuids);

    // Rebuild the hierarchy from an MVF3 document's flat node array.
    // documentNodes  — the parsed document.nodes QJsonArray.
    // sceneRootNodes — the scene's root-level node index array (scene["nodes"]).
    void rebuildFromMvf(const QJsonArray& documentNodes,
                        const QJsonArray& sceneRootNodes);

    // Reset to an empty graph (e.g. on "New scene").
    void clear();

    // Session persistence
    void serialize(QDataStream& out) const;
    bool deserialize(QDataStream& in);

    // -----------------------------------------------------------------------
    // Query
    // -----------------------------------------------------------------------

    // The invisible root node.  Its children are the per-file synthetic nodes.
    // Do not display this node in the tree widget — use root()->children as
    // the top-level items.
    SceneNode* root() const { return _root; }

    // Return the SceneNode that currently owns meshUuid, or nullptr if the
    // mesh has been removed (moved to recycle bin) or does not exist.
    SceneNode* findNodeForMesh(const QUuid& meshUuid) const;

    // Recursively collect all mesh UUIDs under node, including descendants.
    // The order follows a DFS pre-order traversal (node's own meshUuids first,
    // then children left-to-right), which matches the original load order.
    QList<QUuid> collectMeshUuids(const SceneNode* node) const;

    // Evaluate authoritative world transforms from the stored localTransform
    // hierarchy.  The returned maps are keyed by stable nodeUuid / meshUuid.
    SceneGraphWorldTransforms evaluateWorldTransforms(const SceneNode* subtreeRoot = nullptr) const;
    SceneGraphWorldTransforms evaluateWorldTransformsForFile(const QString& sourceFile) const;

    // -----------------------------------------------------------------------
    // KHR_lights_punctual
    // -----------------------------------------------------------------------

    // Register punctual light data for a source file (called after import).
    void setLightData(const QString& sourceFile, const GltfLightData& data);

    // Remove punctual light data when a file's meshes are cleared.
    void clearLightData(const QString& sourceFile);

    // Returns the light data for a specific source file, or an empty struct.
    GltfLightData lightDataForFile(const QString& sourceFile) const;

    // Returns all source files that carry KHR_lights_punctual data.
    QStringList filesWithLights() const;

    // Enable or disable one individual light within a source file.
    // lightIndex is the index into GltfLightData::lights for that file.
    void setLightEnabled(const QString& sourceFile, int lightIndex, bool enabled);

    // Build the flat GPU list from all currently-enabled per-file lights.
    // Call this whenever a light is toggled or a file is added/removed, then
    // pass the result to PunctualLights::setLights().
    std::vector<GPULight> buildEnabledLightList() const;

    SceneNode* findFileNode(const QString& sourceFile) const;

    // Every currently loaded source file (one entry per synthetic top-level
    // file node), regardless of format or whether it carries any
    // KHR_materials_variants data yet - unlike filesWithVariants(), this
    // includes STEP/OBJ/etc. files so the Variants tab can offer "Capture
    // Current as Variant..." on them too, not just files that already
    // arrived with the extension.
    QStringList allSourceFiles() const;

    // -----------------------------------------------------------------------
    // KHR_materials_variants
    // -----------------------------------------------------------------------

    // Register variant data for a source file (called after import succeeds).
    void setVariantData(const QString& sourceFile, const GltfVariantData& data);

    // Remove variant data for a source file.
    void clearVariantData(const QString& sourceFile);

    // Returns the variant data for a specific source file, or an empty struct.
    GltfVariantData variantDataForFile(const QString& sourceFile) const;

    // Returns all source files that carry KHR_materials_variants data.
    QStringList filesWithVariants() const;

    // Track and retrieve the currently active variant per source file.
    // -1 = file default (no variant active).
    void setActiveVariant(const QString& sourceFile, int variantIndex);
    int  activeVariantForFile(const QString& sourceFile) const;

    // -----------------------------------------------------------------------
    // glTF animations
    // -----------------------------------------------------------------------
    void setAnimationData(const QString& sourceFile, const GltfAnimationData& data);
    void clearAnimationData(const QString& sourceFile);
    GltfAnimationData animationDataForFile(const QString& sourceFile) const;
    QStringList filesWithAnimations() const;
    void setActiveAnimationClip(const QString& sourceFile, int clipIndex);
    int activeAnimationClipForFile(const QString& sourceFile) const;

    // -----------------------------------------------------------------------
    // glTF cameras
    // -----------------------------------------------------------------------
    void setGltfCameraData(const QString& sourceFile, const GltfCameraData& data);
    void clearGltfCameraData(const QString& sourceFile);
    GltfCameraData gltfCameraDataForFile(const QString& sourceFile) const;
    QStringList filesWithGltfCameras() const;

    // -----------------------------------------------------------------------
    // Measurements ("Measure" tool). Document-level, not per-file - see
    // MeasurementData.h.
    // -----------------------------------------------------------------------
    void addMeasurement(const Measurement& measurement);
    // Re-inserts at a specific position (undo of removeMeasurementAt) rather
    // than appending, so undo/redo round-trips preserve display order.
    void insertMeasurementAt(int index, const Measurement& measurement);
    void removeMeasurementAt(int index);
    void clearMeasurements();
    const QVector<Measurement>& measurements() const { return _measurements; }
    int measurementIndexById(const QUuid& id) const;
    // Show/hide toggle (the Measurement dialog's results-list checkboxes) -
    // not undoable, same convention as mesh-visibility checkboxes elsewhere.
    void setMeasurementVisible(const QUuid& id, bool visible);

    // Direct mutator for an angle dimension's arc-radius override - called
    // on every mouse-move during a drag (for live preview) AND from
    // MeasurementOffsetCommand::redo()/undo() (for the single commit pushed
    // at drag-end) - see Measurement::offsetDistance's doc comment. Not
    // itself undo-aware; the command class is what makes the FINAL value
    // undoable, same "direct mutator + separate command wraps it" split
    // TransformCommand uses for gizmo drags.
    void setMeasurementOffsetDistance(const QUuid& id, float offsetDistance);

    // Same role as setMeasurementOffsetDistance() above, but for a linear
    // dimension's full offset vector (pivot + extend) - see
    // Measurement::offsetVector's doc comment.
    void setMeasurementOffsetVector(const QUuid& id, const QVector3D& offsetVector);

    // -----------------------------------------------------------------------
    // Mutation  (called by undo/redo command classes)
    // -----------------------------------------------------------------------

    // Remove meshUuid from its owning node and deregister it from the lookup
    // table.  The node itself is NOT deleted even if it becomes empty, so that
    // undo (restoreMeshUuid) can safely put it back without needing to
    // reconstruct the node.
    //
    // outPosition — receives the index the UUID held in node->meshUuids so
    //               the caller can pass it back to restoreMeshUuid for an
    //               exact restoration.
    //
    // Returns the owning SceneNode (caller must store it for undo), or nullptr
    // if the UUID was not found.
    SceneNode* removeMeshUuid(const QUuid& meshUuid, int& outPosition);

    // Re-insert meshUuid into node->meshUuids at position and re-register it
    // in the lookup table.  position is clamped to a valid range automatically
    // in case sibling insertions/removals shifted the list since the removal.
    void restoreMeshUuid(SceneNode* node, const QUuid& meshUuid, int position);

    // Attach newChild under parent at position (appends if position is out of
    // range).  Sets newChild->parent and registers all descendant mesh UUIDs.
    // SceneGraph takes ownership of newChild.
    void insertChildNode(SceneNode* parent, SceneNode* newChild, int position = -1);

    // Detach child from parent->children.  Does NOT free child — the caller
    // (typically PasteCommand) holds ownership until redo re-attaches it.
    // Deregisters all descendant mesh UUIDs.  outPosition receives the index
    // the child was at so redo can restore it exactly.
    void removeChildNode(SceneNode* parent, SceneNode* child, int& outPosition);

    // Return the SceneNode whose nodeUuid matches, or nullptr.  O(n) DFS.
    SceneNode* findNodeByUuid(const QUuid& nodeUuid) const;

    // Detach the file-level node for sourceFile from the root if its subtree
    // no longer owns any mesh UUIDs.  The subtree is NOT freed — ownership
    // passes to the caller (DeleteMeshCommand) so undo can reattach it.
    // Leaving an empty file node in the graph makes findFileNode() resolve to
    // the stale node when the same file is imported again, breaking node
    // transform sync for the re-imported model.
    // outPosition receives the index the node held in root->children.
    // Returns nullptr if no file node exists or it still owns meshes.
    SceneNode* detachEmptyFileNode(const QString& sourceFile, int& outPosition);

    // Reattach a file node previously detached by detachEmptyFileNode().
    void reattachFileNode(SceneNode* fileNode, int position);

    // Delete a subtree that is no longer attached to this SceneGraph.
    // Used by PasteCommand destructor to free cloned nodes when the command
    // is destroyed in the undone state.  Does NOT touch _meshUuidToNode.
    static void deleteDetachedSubtree(SceneNode* root);

signals:
    // Emitted after any structural change (append, clear, remove, restore).
    // The tree widget connects to this to rebuild or refresh its items.
    void structureChanged();

    // Emitted when variant data is added or removed (a file with
    // KHR_materials_variants is loaded or its meshes are cleared).
    // The MaterialVariantsPanel connects to this to refresh its tree.
    void variantDataChanged();
    void animationDataChanged();
    void gltfCameraDataChanged();

    // Emitted when a measurement is added or removed.
    void measurementsChanged();

    // Emitted when punctual light data is added, removed, or an individual
    // light's enabled state changes.  PunctualLightsPanel connects to this
    // to refresh its tree; ViewportWidget connects to rebuild the GPU light list.
    void lightDataChanged();

private:
    // Recursively build a SceneNode subtree that mirrors ainode and its
    // descendants.  cursor advances through uuids as mesh UUIDs are assigned
    // to nodes — the traversal order must match processNode() exactly.
    SceneNode* buildSubtree(const aiNode*       ainode,
                            SceneNode*          parent,
                            const QList<QUuid>& uuids,
                            int&                cursor);

    void collectUuidsRecursive(const SceneNode* node, QList<QUuid>& out) const;

    // Register / deregister every mesh UUID in a subtree in _meshUuidToNode.
    void registerSubtreeUuids(SceneNode* node);
    void deregisterSubtreeUuids(SceneNode* node);

    // Delete node and all of its descendants.  Does not touch _meshUuidToNode;
    // callers are responsible for clearing the hash before calling this.
    void freeSubtree(SceneNode* node);

    // Invisible root — never shown in the UI.
    SceneNode* _root = nullptr;

    // Fast O(1) lookup from mesh UUID to the SceneNode that owns it.
    // Entries are added in appendFromScene / restoreMeshUuid and removed in
    // removeMeshUuid / clear.
    QHash<QUuid, SceneNode*> _meshUuidToNode;

    // KHR_lights_punctual: one entry per source file that carries the extension.
    // Individual GltfLightEntry::enabled flags track per-light activation state.
    QHash<QString, GltfLightData> _lightDataByFile;

    // KHR_materials_variants: one entry per source file that carries the extension.
    QHash<QString, GltfVariantData> _variantDataByFile;

    // Currently active variant index per source file (-1 = file default).
    QHash<QString, int> _activeVariantByFile;
    QHash<QString, GltfAnimationData> _animationDataByFile;
    QHash<QString, int> _activeAnimationClipByFile;

    // glTF cameras: one entry per source file that declares cameras (this
    // includes the synthetic capturedViewsSourceFileKey() bucket).
    QHash<QString, GltfCameraData> _gltfCameraDataByFile;

    // Document-level, not per-file - see MeasurementData.h.
    QVector<Measurement> _measurements;
};
