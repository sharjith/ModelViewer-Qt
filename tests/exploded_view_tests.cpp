// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// GUI-free checks of the hierarchical exploded view: the group tree builders and ExplodedViewManager::recomputeHierarchical.
#include "ExplodedGroupTree.h"
#include "ExplodedViewManager.h"

#include <QSet>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>

// ExplodedViewManager only needs the key function of the relation graph; the graph itself depends on the scene and the viewport.
QString AssemblyRelationGraph::makeMeshPairKey(const QUuid& a, const QUuid& b)
{
    const QString sa = a.toString(QUuid::WithoutBraces), sb = b.toString(QUuid::WithoutBraces);
    return sa < sb ? sa + QLatin1Char('|') + sb : sb + QLatin1Char('|') + sa;
}

namespace
{
int failures = 0;

void check(bool ok, const char* what)
{
    if (!ok)
    {
        ++failures;
        std::cerr << "FAIL: " << what << "\n";
    }
}

using Box = ExplodedGroupTree::Box;

Box cube(float x, float y, float z, float half = 0.5f)
{
    return {QVector3D(x - half, y - half, z - half), QVector3D(x + half, y + half, z + half)};
}

struct Scene
{
    QSet<QUuid> parts;
    QHash<QUuid, Box> boxes;
    QHash<QUuid, QVector3D> centres;
    QUuid add(const Box& b)
    {
        const QUuid id = QUuid::createUuid();
        parts.insert(id);
        boxes.insert(id, b);
        centres.insert(id, (b.first + b.second) * 0.5f);
        return id;
    }
};

// Two sub-assemblies of three parts each, far apart: A around x = 0, B around x = 100.
struct TwoAssemblies
{
    Scene scene;
    QVector<QUuid> a, b;
    ExplodedGroupTree::Tree tree;
    TwoAssemblies()
    {
        for (int i = 0; i < 3; ++i) a.append(scene.add(cube(float(i) * 1.2f, 0, 0)));
        for (int i = 0; i < 3; ++i) b.append(scene.add(cube(100.0f + float(i) * 1.2f, 0, 0)));
        ExplodedGroupTree::Draft root;
        root.id = QUuid::createUuid();
        ExplodedGroupTree::Draft ga, gb;
        ga.id = QUuid::createUuid();
        gb.id = QUuid::createUuid();
        ga.parts = a;
        gb.parts = b;
        root.children.push_back(ga);
        root.children.push_back(gb);
        tree = ExplodedGroupTree::fromDraft(root);
    }
};

float spread(const ExplodedViewManager& m, const QVector<QUuid>& ids, const Scene& s)
{
    float lo = 1e30f, hi = -1e30f;
    for (const QUuid& id : ids)
    {
        const float x = s.centres.value(id).x() + m.offsetForMesh(id).x();
        lo = std::min(lo, x);
        hi = std::max(hi, x);
    }
    return hi - lo;
}

void testCollapse()
{
    using namespace ExplodedGroupTree;
    const QUuid p1 = QUuid::createUuid(), p2 = QUuid::createUuid(), p3 = QUuid::createUuid();
    // root -> wrapper -> wrapper -> {group{p1,p2}, group{p3}, empty}
    Draft inner;
    inner.id = QUuid::createUuid();
    inner.parts = {p1, p2};
    Draft single;
    single.id = QUuid::createUuid();
    single.parts = {p3};
    Draft empty;
    empty.id = QUuid::createUuid();
    Draft w2;
    w2.id = QUuid::createUuid();
    w2.children.push_back(inner);
    w2.children.push_back(single);
    w2.children.push_back(empty);
    Draft w1;
    w1.id = QUuid::createUuid();
    w1.children.push_back(w2);
    Draft root;
    root.id = QUuid::createUuid();
    root.children.push_back(w1);

    const Tree tree = fromDraft(root);
    check(tree.groups.size() == 2, "wrapper chain, empty group collapsed: root plus one sub-group");
    check(tree.partCount() == 3, "no part lost by collapsing");
    check(tree.groups[0].id == w2.id, "the root is the first group that carries information");
    check(tree.groups[0].parts == QVector<QUuid>{p3}, "a single-part group is lifted into its parent");
    check(tree.groups[1].parent == 0 && tree.groups[1].id == inner.id, "pre-order with parent links");
    check(fromDraft(Draft{}).isEmpty(), "an empty draft gives an empty tree");
    check(tree.levelCount() == 2, "root plus one sub-group is two levels");
    check(Tree{}.levelCount() == 0, "an empty tree has no levels");
}

void testFromBoxes()
{
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> d(0.0f, 200.0f);
    Scene s;
    for (int i = 0; i < 1000; ++i) s.add(cube(d(rng), d(rng), d(rng)));
    const auto tree = ExplodedGroupTree::fromBoxes(s.parts, s.boxes, 8);
    check(tree.partCount() == 1000, "every part appears exactly once (count)");
    QSet<QUuid> seen;
    bool leafOk = true, fanOk = true;
    for (const auto& g : tree.groups)
    {
        for (const QUuid& id : g.parts) seen.insert(id);
        if (g.parts.size() > 8 + 3) leafOk = false;   // lifted single parts may add a few to an inner group
        if (g.children.size() > 4) fanOk = false;
    }
    check(seen == s.parts, "every part appears exactly once (identity)");
    check(leafOk, "groups stay small");
    check(fanOk, "at most four children per group");
    check(tree.groups.size() > 1, "a thousand parts are split into groups");

    // Two far clusters end up in different branches.
    Scene two;
    QVector<QUuid> left, right;
    for (int i = 0; i < 20; ++i) left.append(two.add(cube(float(i % 5), float(i / 5), 0)));
    for (int i = 0; i < 20; ++i) right.append(two.add(cube(500.0f + float(i % 5), float(i / 5), 0)));
    const auto t2 = ExplodedGroupTree::fromBoxes(two.parts, two.boxes, 8);
    const auto topOf = [&](const QUuid& id) {
        for (int gi = 0; gi < t2.groups.size(); ++gi)
            if (t2.groups[gi].parts.contains(id))
            {
                int up = gi;
                while (t2.groups[up].parent > 0) up = t2.groups[up].parent;
                return up;
            }
        return -1;
    };
    bool separated = true;
    for (const QUuid& l : left)
        for (const QUuid& r : right)
            if (topOf(l) == topOf(r)) separated = false;
    check(separated, "far-apart clusters are not mixed in one top-level branch");
}

void testHierarchicalLayout()
{
    TwoAssemblies f;
    ExplodedViewManager m;
    m.recomputeHierarchical(f.tree, QUuid(), 1.0f, f.scene.centres, f.scene.boxes);
    check(m.isActive(), "layout produced offsets");

    // Siblings in one sub-assembly stay far closer to each other than to the other sub-assembly.
    const float spreadA = spread(m, f.a, f.scene);
    const float gapAB = (f.scene.centres.value(f.b[0]).x() + m.offsetForMesh(f.b[0]).x())
                      - (f.scene.centres.value(f.a[2]).x() + m.offsetForMesh(f.a[2]).x());
    check(spreadA < 30.0f, "a sub-assembly explodes locally");
    check(gapAB > spreadA, "sub-assemblies remain separated by more than their own spread");

    // maxDepth 1: only the root level explodes, so the parts of one sub-assembly share one offset.
    ExplodedViewManager rigid;
    rigid.recomputeHierarchical(f.tree, QUuid(), 1.0f, f.scene.centres, f.scene.boxes, nullptr, 1);
    check((rigid.offsetForMesh(f.a[0]) - rigid.offsetForMesh(f.a[2])).length() < 1e-5f, "depth 1 moves a sub-assembly rigidly");
    check(spread(rigid, f.a, f.scene) < spread(m, f.a, f.scene) + 1e-4f, "depth 1 never spreads a sub-assembly more than the full layout");

    // Anchor stays put, and so does the sub-assembly that holds it (at the top level).
    ExplodedViewManager anchored;
    anchored.recomputeHierarchical(f.tree, f.a[1], 1.0f, f.scene.centres, f.scene.boxes);
    check(anchored.offsetForMesh(f.a[1]).length() < 1e-5f, "the anchor part does not move");

    // The offsets are linear in the factor.
    ExplodedViewManager half;
    half.recomputeHierarchical(f.tree, QUuid(), 0.5f, f.scene.centres, f.scene.boxes);
    check((half.offsetForMesh(f.b[2]) * 2.0f - m.offsetForMesh(f.b[2])).length() < 1e-4f, "offsets scale linearly with the factor");
    m.setFactor(0.5f);
    check((half.offsetForMesh(f.b[2]) - m.offsetForMesh(f.b[2])).length() < 1e-5f, "setFactor equals a recompute at that factor");

    // Deterministic.
    ExplodedViewManager again;
    again.recomputeHierarchical(f.tree, QUuid(), 0.5f, f.scene.centres, f.scene.boxes);
    bool same = true;
    for (const QUuid& id : f.scene.parts)
        if ((again.offsetForMesh(id) - half.offsetForMesh(id)).length() > 1e-6f) same = false;
    check(same, "the same input gives the same offsets");
}

void testSingleGroupLayout()
{
    Scene s;
    for (int i = 0; i < 12; ++i) s.add(cube(float(i % 4) * 3.0f, float(i / 4) * 3.0f, float(i % 3) * 2.0f));
    ExplodedGroupTree::Draft root;
    root.id = QUuid::createUuid();
    for (const QUuid& id : s.parts) root.parts.append(id);
    const auto tree = ExplodedGroupTree::fromDraft(root);
    check(tree.groups.size() == 1, "a flat draft stays one group");

    ExplodedViewManager hier;
    hier.recomputeHierarchical(tree, QUuid(), 1.0f, s.centres, s.boxes);
    // Proportional layout: every part moves away from the centre of the group by its own distance from it (gain 1 at the top level).
    QVector3D lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
    for (const QUuid& id : s.parts)
    {
        const Box b = s.boxes.value(id);
        lo = QVector3D(std::min(lo.x(), b.first.x()), std::min(lo.y(), b.first.y()), std::min(lo.z(), b.first.z()));
        hi = QVector3D(std::max(hi.x(), b.second.x()), std::max(hi.y(), b.second.y()), std::max(hi.z(), b.second.z()));
    }
    const QVector3D centre = (lo + hi) * 0.5f;
    bool same = true;
    for (const QUuid& id : s.parts)
        if ((hier.offsetForMesh(id) - (s.centres.value(id) - centre)).length() > 1e-4f) same = false;
    check(same, "one group scales its parts proportionally about its centre");
}
}

int main()
{
    testCollapse();
    testFromBoxes();
    testHierarchicalLayout();
    testSingleGroupLayout();
    if (failures == 0)
        std::cout << "exploded_view_tests: all passed\n";
    return failures == 0 ? 0 : 1;
}
