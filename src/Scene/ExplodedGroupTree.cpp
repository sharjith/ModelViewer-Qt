// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#include "ExplodedGroupTree.h"

#include <algorithm>
#include <utility>

namespace ExplodedGroupTree
{

int Tree::partCount() const
{
    int count = 0;
    for (const Group& g : groups)
        count += g.parts.size();
    return count;
}

namespace
{

void collapse(Draft& d)
{
    std::vector<Draft> kept;
    kept.reserve(d.children.size());
    for (Draft& c : d.children)
    {
        collapse(c);
        if (c.children.empty() && c.parts.isEmpty())
            continue;
        if (c.children.empty() && c.parts.size() == 1)
        {
            d.parts.append(c.parts.first());
            continue;
        }
        if (c.parts.isEmpty() && c.children.size() == 1)
        {
            kept.push_back(std::move(c.children.front()));
            continue;
        }
        kept.push_back(std::move(c));
    }
    d.children = std::move(kept);
}

int flatten(const Draft& d, int parent, Tree& tree)
{
    const int index = tree.groups.size();
    tree.groups.push_back(Group{});
    tree.groups[index].id = d.id;
    tree.groups[index].parent = parent;
    tree.groups[index].parts = d.parts;
    for (const Draft& c : d.children)
    {
        const int childIndex = flatten(c, index, tree);
        tree.groups[index].children.push_back(childIndex);
    }
    return index;
}

struct Item
{
    QUuid id;
    QVector3D centre;
};

float axisValue(const QVector3D& v, int axis)
{
    return axis == 0 ? v.x() : (axis == 1 ? v.y() : v.z());
}

// Splits items[lo, hi) into up to 2^levels ranges (ordered along the axes used), reordering the items in place.
void splitRange(std::vector<Item>& items, size_t lo, size_t hi, int levels, std::vector<std::pair<size_t, size_t>>& out)
{
    const size_t n = hi - lo;
    if (levels == 0 || n < 2)
    {
        out.emplace_back(lo, hi);
        return;
    }

    QVector3D mn = items[lo].centre, mx = items[lo].centre;
    for (size_t i = lo + 1; i < hi; ++i)
    {
        const QVector3D& c = items[i].centre;
        mn = QVector3D(std::min(mn.x(), c.x()), std::min(mn.y(), c.y()), std::min(mn.z(), c.z()));
        mx = QVector3D(std::max(mx.x(), c.x()), std::max(mx.y(), c.y()), std::max(mx.z(), c.z()));
    }
    const QVector3D extent = mx - mn;
    int axis = 0;
    if (extent.y() > axisValue(extent, axis)) axis = 1;
    if (extent.z() > axisValue(extent, axis)) axis = 2;

    std::sort(items.begin() + static_cast<std::ptrdiff_t>(lo), items.begin() + static_cast<std::ptrdiff_t>(hi),
              [axis](const Item& a, const Item& b) {
                  const float va = axisValue(a.centre, axis), vb = axisValue(b.centre, axis);
                  if (va != vb) return va < vb;
                  return a.id.toString(QUuid::WithoutBraces) < b.id.toString(QUuid::WithoutBraces);
              });

    // Cut at the largest gap between neighbouring centres in the middle half; with no gap at all, in the middle.
    const size_t margin = std::max<size_t>(1, n / 4);
    size_t first = lo + margin, last = hi - margin;
    if (first > last) first = last = lo + n / 2;
    size_t cut = lo + n / 2;
    float bestGap = axisValue(items[cut].centre, axis) - axisValue(items[cut - 1].centre, axis);
    for (size_t p = first; p <= last; ++p)
    {
        const float gap = axisValue(items[p].centre, axis) - axisValue(items[p - 1].centre, axis);
        if (gap > bestGap)
        {
            bestGap = gap;
            cut = p;
        }
    }

    splitRange(items, lo, cut, levels - 1, out);
    splitRange(items, cut, hi, levels - 1, out);
}

void buildNode(std::vector<Item>& items, size_t lo, size_t hi, Draft& into, size_t leafSize)
{
    if (hi - lo <= leafSize)
    {
        for (size_t i = lo; i < hi; ++i)
            into.parts.append(items[i].id);
        return;
    }
    std::vector<std::pair<size_t, size_t>> ranges;
    splitRange(items, lo, hi, 2, ranges);
    for (const auto& r : ranges)
    {
        Draft child;
        child.id = QUuid::createUuid();
        buildNode(items, r.first, r.second, child, leafSize);
        into.children.push_back(std::move(child));
    }
}

}

Tree fromDraft(Draft draft)
{
    collapse(draft);
    // A root that is only a wrapper is skipped too.
    while (draft.parts.isEmpty() && draft.children.size() == 1)
    {
        Draft only = std::move(draft.children.front());
        draft = std::move(only);
    }
    Tree tree;
    if (draft.parts.isEmpty() && draft.children.empty())
        return tree;
    flatten(draft, -1, tree);
    return tree;
}

Tree fromBoxes(const QSet<QUuid>& parts, const QHash<QUuid, Box>& boxes, int leafSize)
{
    std::vector<Item> items;
    items.reserve(static_cast<size_t>(parts.size()));
    Draft root;
    root.id = QUuid::createUuid();
    for (const QUuid& uuid : parts)
    {
        const auto it = boxes.constFind(uuid);
        if (it == boxes.constEnd())
            root.parts.append(uuid);
        else
            items.push_back({uuid, (it->first + it->second) * 0.5f});
    }
    // QSet order is not stable between runs; the sort in splitRange breaks ties by UUID, and this makes the starting order canonical.
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return a.id.toString(QUuid::WithoutBraces) < b.id.toString(QUuid::WithoutBraces);
    });
    buildNode(items, 0, items.size(), root, static_cast<size_t>(std::max(2, leafSize)));
    return fromDraft(std::move(root));
}

}
