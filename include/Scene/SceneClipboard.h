#pragma once

#include <QList>
#include <QString>
#include <QUuid>
// types.h (not matrix4x4.h alone) is needed for aiMatrix4x4t's inline
// constructor definitions (matrix4x4.h only declares them).
#include <assimp/types.h>

// ---------------------------------------------------------------------------
// ClipboardNode
//
// Lightweight, pointer-free snapshot of one SceneNode for clipboard storage.
// Holds original mesh UUIDs (sources to clone from at paste time) and
// recursively mirrors the subtree structure.  Safe to keep across scene
// modifications because it holds no live pointers.
// ---------------------------------------------------------------------------
struct ClipboardNode
{
    QString      name;
    aiMatrix4x4  localTransform;
    QList<QUuid> meshUuids;         // original UUIDs — cloned at paste time
    QList<ClipboardNode> children;
};

// ---------------------------------------------------------------------------
// ClipboardEntry
//
// One top-level item placed on the clipboard by a Copy or Cut action.
//
//   isCut == false (Copy):
//     isLeaf == true  → leafUuid holds the mesh UUID to clone.
//     isLeaf == false → assemblyRoot holds a deep snapshot to clone.
//
//   isCut == true (Cut):
//     Items remain in the scene until Paste; they are only visually grayed.
//     Source location is stored as UUIDs (resolved to live pointers at paste
//     time) so the clipboard stays valid across incidental structureChanged
//     signals that don't actually remove the cut items.
//
//     isLeaf == true:
//       leafUuid           — the mesh UUID (same UUID reused by Paste).
//       cutSourceNodeUuid  — nodeUuid of the ownerNode that currently holds
//                            the mesh.
//       cutSourcePosition  — index within ownerNode->meshUuids.
//
//     isLeaf == false:
//       cutNodeUuid        — nodeUuid of the subtree root itself.
//       cutSourceNodeUuid  — nodeUuid of the subtree root's current parent.
//       cutSourcePosition  — index within parent->children.
// ---------------------------------------------------------------------------
struct ClipboardEntry
{
    bool isLeaf = false;
    bool isCut  = false;

    // Copy — valid when isLeaf == true
    QUuid leafUuid;

    // Copy — valid when isLeaf == false
    ClipboardNode assemblyRoot;

    // Cut source location (both leaf and assembly)
    QUuid cutSourceNodeUuid;   // ownerNode UUID (leaf) or parent UUID (assembly)
    QUuid cutNodeUuid;         // subtree root UUID (assembly only)
    int   cutSourcePosition = 0;
};
