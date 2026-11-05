// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QHash>
#include <QPair>
#include <QSet>
#include <QUuid>
#include <QVector>
#include <QVector3D>

#include <vector>

// ---------------------------------------------------------------------------
// ExplodedGroupTree
//
// The hierarchy the exploded view lays out level by level: a group holds parts (mesh UUIDs) and sub-groups, and every group is
// moved as one rigid block among its siblings. Two builders produce it:
//   - the scene tree (Assembly-aware strategy), see AssemblyRelationGraph::buildSceneGroupTree, and
//   - a spatial hierarchy over the part boxes (Classic strategy), see fromBoxes below.
// Both go through fromDraft, which collapses wrapper nodes that carry no layout information. QtCore + QtGui only.
// ---------------------------------------------------------------------------
namespace ExplodedGroupTree
{

using Box = QPair<QVector3D, QVector3D>;

struct Group
{
    QUuid id;                  // identity of this group as a block among its siblings (a scene node id, or generated)
    int parent = -1;           // index into Tree::groups, -1 for the root
    QVector<int> children;     // indices into Tree::groups
    QVector<QUuid> parts;      // mesh UUIDs placed directly in this group
};

struct Tree
{
    // Pre-order: a group always follows its parent, groups[0] is the root.
    QVector<Group> groups;

    bool isEmpty() const { return groups.isEmpty(); }
    int partCount() const;
};

// Recursive form the builders fill in before collapsing.
struct Draft
{
    QUuid id;
    QVector<QUuid> parts;
    std::vector<Draft> children;
};

// Collapses what carries no information and flattens into a Tree:
//   - an empty group disappears,
//   - a group holding a single part and nothing else is replaced by that part in its parent,
//   - a group with no parts and a single child is replaced by the child (wrapper chains of CAD exports).
// The root is collapsed the same way, so a deep wrapper chain above the real assembly is skipped.
Tree fromDraft(Draft draft);

// Classic strategy: a spatial hierarchy over the part boxes. Parts are split at the largest gap along the longest axis of their
// centre points (searched in the middle half so the halves stay balanced), two binary splits per level (up to four children), until
// a group holds at most leafSize parts. Parts without a box are placed in the root.
Tree fromBoxes(const QSet<QUuid>& parts, const QHash<QUuid, Box>& boxes, int leafSize = 8);

}
