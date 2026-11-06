#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Test whether shape i of one LOD alternative is the same body as shape i of another, from `jt_probe --lod-shapes` output.

Read-only; nothing is decoded and no model string is kept. For every LOD node whose alternatives hold the same number of shapes
it compares the untransformed bounding box of shape i in the finest alternative (the one whose shapes name the lowest Shape LOD
segment type) with shape i in every other alternative. Boxes of different detail levels of one body should agree closely; a
box is judged to agree when every corner differs by at most `tolerance` times the larger box diagonal.

Per input it reports how many LOD nodes and shapes agree, how many do not, how many nodes fall in each class, and whether a
different order would have matched (shape i of one alternative agrees with shape j of the other, i != j). When the input
also carries the stored shape area, a node is classed confirmed_unique (every positional pair agrees on box and area and no other
shape does), ambiguous (a pair agrees but so does another shape) or contradicted (a positional pair disagrees).
"""
import argparse
from collections import Counter
import json
import math
from pathlib import Path
import sys


def diagonal(box):
    return math.dist(box[:3], box[3:])


def agrees(a, b, tolerance):
    scale = max(diagonal(a), diagonal(b), 1e-9)
    return all(abs(x - y) <= tolerance * scale for x, y in zip(a, b))


def area_agrees(a, b, tolerance):
    return abs(a - b) <= tolerance * max(a, b, 1e-12)


def level(shape):
    types = [t for t in shape['segmentTypes'] if 7 <= t <= 16]
    return min(types) if types else 0


def analyse(document, tolerance, area_tolerance):
    counts = Counter()
    worst = 0.0
    for node in document['lodNodes']:
        alternatives = [a['shapes'] for a in node['alternatives']]
        counts['lod_nodes'] += 1
        if len(alternatives) < 2:
            counts['single_alternative'] += 1
            continue
        sizes = {len(a) for a in alternatives}
        if len(sizes) != 1:
            counts['shape_counts_differ'] += 1
            continue
        levels = [min((level(s) or 99 for s in a), default=99) for a in alternatives]
        finest = levels.index(min(levels))
        reference = alternatives[finest]
        node_ok, reordered = True, False
        for k, other in enumerate(alternatives):
            if k == finest:
                continue
            for i, shape in enumerate(other):
                box_a, box_b = reference[i]['bounds'], shape['bounds']
                counts['shape_pairs'] += 1
                scale = max(diagonal(box_a), diagonal(box_b), 1e-9)
                worst = max(worst, max(abs(x - y) for x, y in zip(box_a, box_b)) / scale)
                if agrees(box_a, box_b, tolerance):
                    counts['shape_pairs_agree'] += 1
                else:
                    counts['shape_pairs_disagree'] += 1
                    node_ok = False
                    if any(j != i and agrees(reference[j]['bounds'], box_b, tolerance) for j in range(len(reference))):
                        reordered = True
        counts['nodes_all_pairs_agree' if node_ok else 'nodes_with_disagreement'] += 1
        if all('area' in s for a in alternatives for s in a):
            # Box plus stored surface area: a positional pair is confirmed when both agree, and unique when no other shape of the
            # reference alternative agrees with it on both (so a swap of two shapes would have been noticed).
            status = 'confirmed_unique'
            for k, other in enumerate(alternatives):
                if k == finest:
                    continue
                for i, shape in enumerate(other):
                    same = [j for j, ref in enumerate(reference) if agrees(ref['bounds'], shape['bounds'], tolerance)
                            and area_agrees(ref['area'], shape['area'], area_tolerance)]
                    if i not in same:
                        status = 'contradicted'
                        break
                    if len(same) > 1 and status == 'confirmed_unique':
                        status = 'ambiguous'
                if status == 'contradicted':
                    break
            counts['nodes_positional_match_' + status] += 1
            if len(reference) > 1:
                counts['multi_shape_nodes_positional_match_' + status] += 1
        if reordered:
            counts['nodes_where_another_order_matches'] += 1
    return {'tolerance_fraction_of_diagonal': tolerance, 'area_tolerance_fraction': area_tolerance, 'counts': dict(counts), 'largest_corner_difference_fraction': round(worst, 6)}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('input', type=Path, help='output of jt_probe --lod-shapes')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--tolerance', type=float, default=0.05)
    parser.add_argument('--area-tolerance', type=float, default=0.35, help='relative difference of the stored shape areas')
    args = parser.parse_args()
    try:
        report = analyse(json.loads(args.input.read_text(encoding='utf-8')), args.tolerance, args.area_tolerance)
    except (OSError, ValueError, KeyError) as error:
        print(f'error: {error}', file=sys.stderr)
        return 1
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=1), encoding='utf-8')
    print(json.dumps(report, indent=1))
    return 0


if __name__ == '__main__':
    sys.exit(main())
