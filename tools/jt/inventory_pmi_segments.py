#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Inventory every PMI data segment in one little-endian JT 10.x file.

The report intentionally omits filenames, string-table contents, property values
and polyline coordinates. Keep generated JSON under ignored out/.

Usage:
  python -B tools/jt/inventory_pmi_segments.py INPUT.jt --output out/jt-research/pmi-inventory.json
"""
import argparse
from collections import Counter
import hashlib
import json
import lzma
from pathlib import Path
import re
import struct
import sys

from decode_pmi_segment import decode


def inventory(path):
    data = path.read_bytes()
    match = re.match(rb'Version\s+(\d+)\.(\d+)', data[:80])
    if not match or len(data) < 109:
        raise ValueError('Not a complete JT header')
    major, minor = map(int, match.groups())
    if major != 10:
        raise ValueError('This PMI inventory currently supports JT 10.x containers only')
    if data[80] != 0:
        raise ValueError('This PMI inventory currently supports little-endian files only')
    toc = struct.unpack_from('<q', data, 85)[0]
    if toc < 0 or toc + 4 > len(data):
        raise ValueError('Invalid table-of-contents offset')
    count = struct.unpack_from('<i', data, toc)[0]
    if count < 1 or count > 1_000_000 or toc + 4 + count * 32 > len(data):
        raise ValueError('Invalid table-of-contents count/range')

    rows = []
    versions = Counter()
    kinds = Counter()
    dimension_types = Counter()
    dimension_styles = Counter()
    property_keys = Counter()
    for index in range(count):
        entry = toc + 4 + index * 32
        offset, length, attributes = struct.unpack_from('<qiI', data, entry + 16)
        if attributes >> 24 != 3:
            continue
        if offset < 0 or length < 24 or offset + length > len(data):
            raise ValueError('Invalid PMI segment range')
        raw = data[offset:offset + length]
        start = raw.find(b'\xfd7zXZ')
        if start < 0:
            raise ValueError('PMI segment is not LZMA-compressed')
        decoded = lzma.decompress(raw[start:])
        result = decode(decoded)
        versions[str(result['version'])] += 1
        vertices = 0
        text_entities = 0
        for entity in result['entities']:
            kind = entity['entity_type_name']
            kinds[kind] += 1
            vertices += entity['polyline']['vertex_count']
            text_entities += len(entity['text_entities'])
            properties = {p['key']: p['value'] for p in entity['properties']}
            property_keys.update(properties.keys())
            if kind == 'Dimension':
                dimension_types[properties.get('type', '<missing>')] += 1
                dimension_styles[properties.get('style', '<missing>')] += 1
        rows.append({
            'toc_index': index,
            'element_version': result['version'],
            'decoded_bytes': len(decoded),
            'entities': len(result['entities']),
            'polyline_vertices': vertices,
            'text_entities': text_entities,
            'trailing_bytes': result['trailing_bytes'],
        })

    return {
        'tool_schema': 1,
        'sha256': hashlib.sha256(data).hexdigest(),
        'jt_version': f'{major}.{minor}',
        'byte_order': 'LE',
        'pmi_segments': len(rows),
        'element_versions': dict(sorted(versions.items())),
        'entities': sum(row['entities'] for row in rows),
        'entity_kinds': dict(sorted(kinds.items())),
        'polyline_vertices': sum(row['polyline_vertices'] for row in rows),
        'text_entities': sum(row['text_entities'] for row in rows),
        'dimension_types': dict(sorted(dimension_types.items())),
        'dimension_styles': dict(sorted(dimension_styles.items())),
        'property_keys': dict(sorted(property_keys.items())),
        'segments': rows,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('input', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    report = inventory(args.input)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('PMI segments %d, versions %s, entities %d, polyline vertices %d' % (
        report['pmi_segments'], report['element_versions'], report['entities'], report['polyline_vertices']))
    return 0


if __name__ == '__main__':
    sys.exit(main())
