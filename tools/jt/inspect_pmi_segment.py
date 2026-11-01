#!/usr/bin/env python3
"""Read-only structural dump of the PMI data segment (segment type 3) of a JT file.

Exploratory tool, not a decoder: it assumes the LZMA-compressed segment layout seen in Solid Edge 2026 JT 10.10 exports,
lists the string table, then groups the length-prefixed UTF-16 key/value property strings into one entity per group (a
heuristic: groups are separated by binary polyline data). It also counts 3D float triples in the polyline regions. Use it
to cross-check the vendored PMI reader, not as evidence of decoder correctness.

Usage: python -B tools/jt/inspect_pmi_segment.py INPUT.jt --output out/jt-research/<topic>/pmi.json
Nothing is written to the input; the model's geometry and strings are written only to the --output file (keep it under out/).
"""
import argparse
import json
import lzma
import math
import struct
import sys

SEGMENT_PMI = 3
KEYS = {'textDirection', 'underline', 'textHeight', 'linearPlacement', 'textAspect', 'textRotationPoint', 'lineFactor',
        'value', 'textOrigin', 'standard', 'precision', 'unit', 'font', 'type', 'valid', 'justification', 'attachmentType',
        'tolerancePrecision', 'spaceFactor', 'zeroToleranceDisplay', 'textThickness', 'dimensionTrailingZero',
        'symbolColour', 'textColour', 'PMIGeometryColor', 'PMITextForegroundColor', 'diameterPlacement', 'style'}
LEADER_KEYS = {'colour', 'arrowPlacement', 'arrowLineType', 'lineType', 'extensionLineColour', 'extensionLineGap',
               'extensionLineExtension', 'terminator', 'arrowLineThickness', 'thickness', 'arrowColour', 'arrowType',
               'Reference', 'lineTextGap'}
KEYS |= {'Leader[%d].%s' % (n, k) for n in (0, 1) for k in LEADER_KEYS}


def toc(data):
    offset = struct.unpack_from('<q', data, 85)[0]
    count = struct.unpack_from('<i', data, offset)[0]
    position = offset + 4
    entries = []
    for _ in range(count):
        seg_offset, length, attributes = struct.unpack_from('<qiI', data, position + 16)
        position += 32
        entries.append((attributes >> 24, seg_offset, length))
    return entries


def pmi_segment(data):
    for kind, offset, length in toc(data):
        if kind == SEGMENT_PMI:
            raw = data[offset:offset + length]
            start = raw.find(b'\xfd7zXZ')
            if start < 0:
                raise SystemExit('PMI segment is not LZMA-compressed; layout not supported by this tool')
            return lzma.LZMADecompressor().decompress(raw[start:])
    return None


def utf16_strings(blob, start):
    found, i = [], start
    while i < len(blob) - 8:
        length = struct.unpack_from('<i', blob, i)[0]
        if 0 < length < 400 and i + 4 + 2 * length <= len(blob):
            raw = blob[i + 4:i + 4 + 2 * length]
            if all(32 <= raw[k] < 127 and raw[k + 1] == 0 for k in range(0, len(raw), 2)):
                found.append((i, raw.decode('utf-16le'), i + 4 + 2 * length))
                i += 4 + 2 * length
                continue
        i += 1
    return found


def vec(text):
    try:
        return [float(v) for v in text.split()]
    except (AttributeError, ValueError):
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('input')
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    data = open(args.input, 'rb').read()
    blob = pmi_segment(data)
    if blob is None:
        json.dump({'pmi_segment': False}, open(args.output, 'w'), indent=1)
        print('no PMI segment')
        return
    table_count = struct.unpack_from('<i', blob, 0x28)[0]
    position, table = 0x2c, []
    for _ in range(table_count):
        length = struct.unpack_from('<i', blob, position)[0]
        table.append(blob[position + 4:position + 4 + 2 * length].decode('utf-16le'))
        position += 4 + 2 * length
    strings = utf16_strings(blob, position)
    groups = [[strings[0]]]
    for a, b in zip(strings, strings[1:]):
        if b[0] - a[2] > 60:
            groups.append([])
        groups[-1].append(b)
    entities = []
    for index, group in enumerate(groups):
        texts = [s for _, s, _ in group]
        props, other, j = {}, [], 0
        while j < len(texts):
            if texts[j] in KEYS and j + 1 < len(texts):
                props[texts[j]] = texts[j + 1]
                j += 2
            else:
                other.append(texts[j])
                j += 1
        entity = {'name_guess': None, 'properties': props, 'unpaired': other}
        t0, t1 = vec(props.get('Leader[0].terminator')), vec(props.get('Leader[1].terminator'))
        value = float(props['value']) if 'value' in props else None
        if t0 and t1 and value is not None:
            entity['terminator_distance'] = math.dist(t0, t1)
            entity['value_minus_distance'] = value - entity['terminator_distance']
        entities.append(entity)
    names = [t for t in table if t not in ('Dimension', 'PMI')]
    for entity, name in zip(entities, names):
        entity['name_guess'] = name  # positional: table names and groups do not always pair one to one
    json.dump({'pmi_segment': True, 'decoded_bytes': len(blob), 'string_table': table, 'entity_groups': len(groups),
               'entities': entities}, open(args.output, 'w'), indent=1)
    print('PMI segment: %d bytes decoded, %d table strings, %d entity groups (names are positional guesses)' % (len(blob), len(table), len(groups)))
    for entity in entities:
        p = entity['properties']
        print('%-18s type=%s value=%s unit=%s precision=%s terminator distance=%s' % (
            entity['name_guess'], p.get('type'), p.get('value'), p.get('unit'), p.get('precision'),
            None if 'terminator_distance' not in entity else '%.9g' % entity['terminator_distance']))


if __name__ == '__main__':
    sys.exit(main())
