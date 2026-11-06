#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Compare the materials of the alternatives of every Range LOD node, from `jt_probe --source-attributes` output.

Read-only. Input is the JSON that `jt_probe --source-attributes <file.jt>` prints (unaccumulated attributes of the file's own
scene graph; external partitions are not followed). Nothing is decoded and no model string is kept in the report: parts are
counted, never named.

Per input it reports, for LOD nodes with exactly three alternatives:
  * whether the alternatives hold the same number of shapes (a positional shape-to-shape match is only plausible then);
  * whether the materials carried by the shapes of alternative 0, 1 and 2 agree, in order;
  * for the nodes where they do not: how many shapes, how many distinct colours, and where the finest alternative gets its
    colour from (a material on the shape itself, or inherited from an ancestor above the Part).
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import sys


def colour(material):
    return tuple(round(x, 3) for x in material.get('diffuse', ()))


def analyse(nodes):
    parent, stack = {}, []
    for index, node in enumerate(nodes):
        while stack and nodes[stack[-1]]['depth'] >= node['depth']:
            stack.pop()
        parent[index] = stack[-1] if stack else None
        stack.append(index)

    def children(index):
        depth, out, j = nodes[index]['depth'], [], index + 1
        while j < len(nodes) and nodes[j]['depth'] > depth:
            if nodes[j]['depth'] == depth + 1:
                out.append(j)
            j += 1
        return out

    def subtree(index):
        depth, j = nodes[index]['depth'], index + 1
        while j < len(nodes) and nodes[j]['depth'] > depth:
            j += 1
        return list(range(index, j))

    def shapes(index):
        return [j for j in subtree(index) if nodes[j]['type'].startswith('JtNode_Shape')]

    def materials(index):
        return [colour(a) for j in shapes(index) for a in nodes[j]['attributes'] if a['type'] == 'JtAttribute_Material']

    def has_palette(index):
        return any(a['type'] == 'JtAttribute_PaletteMap' for j in shapes(index) for a in nodes[j]['attributes'])

    lods = [i for i, n in enumerate(nodes) if n['type'] in ('JtNode_RangeLOD', 'JtNode_LOD')]
    report = Counter()
    differing_shapes, differing_colours = Counter(), Counter()
    sources = Counter()
    for lod in lods:
        alternatives = children(lod)
        report['lod_nodes'] += 1
        if len(alternatives) != 3:
            report['not_three_alternatives'] += 1
            continue
        counts = [len(shapes(a)) for a in alternatives]
        if len(set(counts)) != 1:
            report['shape_counts_differ'] += 1
            continue
        report['shape_counts_equal'] += 1
        sequences = [materials(a) for a in alternatives]
        if any(has_palette(a) for a in alternatives):
            report['palette_map_present'] += 1
        if sequences[0] == sequences[1] == sequences[2]:
            report['materials_identical'] += 1
            continue
        report['materials_differ'] += 1
        report['differ_0_vs_1'] += sequences[0] != sequences[1]
        report['differ_0_vs_2'] += sequences[0] != sequences[2]
        differing_shapes[counts[0]] += 1
        differing_colours[len(set(sequences[2]))] += 1
        own = bool(sequences[0])
        ancestor, p = False, parent[lod]
        while p is not None and not ancestor:
            ancestor = any(a['type'] == 'JtAttribute_Material' and not a.get('ignored') for a in nodes[p]['attributes'])
            p = parent[p]
        sources['finest: material on the shape' if own else 'finest: inherited from an ancestor' if ancestor else 'finest: none (default)'] += 1
        report['coarse_colours_all_distinct_within_part'] += len(set(sequences[2])) == len(sequences[2])
    return {
        'counts': dict(report),
        'differing_nodes_by_shape_count': dict(sorted(differing_shapes.items())),
        'differing_nodes_by_distinct_coarse_colours': dict(sorted(differing_colours.items())),
        'finest_colour_source_of_differing_nodes': dict(sources),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('input', type=Path, help='output of jt_probe --source-attributes')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        document = json.loads(args.input.read_text(encoding='utf-8'))
        report = analyse(document['nodes'])
    except (OSError, ValueError, KeyError) as error:
        print(f'error: {error}', file=sys.stderr)
        return 1
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=1), encoding='utf-8')
    print(json.dumps(report, indent=1))
    return 0


if __name__ == '__main__':
    sys.exit(main())
