#pragma once

#include <QList>
#include <QString>
#include <QUuid>
// types.h (not matrix4x4.h alone) is needed for aiMatrix4x4t's inline
// constructor definitions (matrix4x4.h only declares them).
#include <assimp/types.h>
#include "LengthUnits.h"

// ---------------------------------------------------------------------------
// SceneNode
//
// A single node in the scene hierarchy mirror.  One SceneNode is created for
// every aiNode encountered during model loading, plus one synthetic
// (isSynthetic == true) wrapper node per imported file that sits above the
// aiNode tree and carries the source filename as its display name.
//
// Ownership: SceneGraph owns all SceneNode instances and is responsible for
// their allocation and deletion.  Raw pointers are used intentionally because
// the tree is fully managed by SceneGraph — callers must not delete nodes.
// ---------------------------------------------------------------------------
struct SceneNode
{
    // Stable identity for this structural node (distinct from mesh UUIDs).
    QUuid nodeUuid;

    // Display name shown in the tree widget.
    QString name;

    // True only for the file-level wrapper node that sits above the aiNode
    // tree.  Synthetic nodes have no corresponding aiNode in _globalScene.
    bool isSynthetic = false;

    // Absolute path of the source file.  Populated only on synthetic nodes.
    QString sourceFile;

    // Import-time coordinate/scale correction applied to the Assimp root node's
    // mTransformation (autoOrient + autoScale).  Stored on the synthetic fileNode so
    // that exporters can factor it out and produce uncorrected output.
    // Identity (default) means no correction was applied.
    aiMatrix4x4 importCorrection;

    // Explicit flags recording which corrections the loader applied at import time.
    // These are the authoritative signal used to recover importCorrection when loading
    // an older MVF that did not persist the full matrix.  The flags are more reliable
    // than heuristically analysing the matrix structure.
    bool autoOrientApplied = false;
    bool autoScaleApplied  = false;

    // Real-world length unit this file's coordinates are authored in - see
    // resolveEffectiveImportUnit()'s own doc comment for the full resolution
    // order this participates in. Unknown means nothing has ever set this
    // (import-time auto-detection and manual correction both write here,
    // via the same fields - see LengthUnits.h).
    LengthUnit importUnit = LengthUnit::Unknown;
    // Distinguishes "nothing has ever set this" from "the user explicitly
    // confirmed/corrected it" - lets a future importer-side auto-detector
    // (STEP header unit, glTF-is-always-meters) skip a node the user
    // already fixed, without needing a third enum state on importUnit
    // itself.
    bool importUnitUserOverridden = false;

    // Local transform copied from aiNode::mTransformation at build time.
    // Identity matrix for synthetic nodes.
    aiMatrix4x4 localTransform;

    // Tree links — managed exclusively by SceneGraph.
    SceneNode*        parent   = nullptr;
    QList<SceneNode*> children;

    // Ordered list of SceneMesh UUIDs that belong to this node.
    // Leaf nodes (those with meshes) carry entries here; pure assembly nodes
    // (containers) have an empty list and rely on their children.
    QList<QUuid> meshUuids;
};
