#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Read-only JT inventory of LOD alternatives, without geometry export or model strings.

Reads only the table of contents and, where the root logical scene graph is stored without a
profile this tool does not decode, nothing more. Per input it reports:
  * on-disk bytes and count of shape segments by segment type (JT 10.6 reference, section 4,
    NOTE 1: types 7-16 are Shape LOD segments, 7 the most detailed; 6 is the generic shape type);
  * for each Range LOD node element of the root scene graph (when its zlib payload can be read):
    the Range Limits and Centre, which are the last two fields of the element, so they are read from
    the end of the element payload.
Uses only the standard library. Does not follow external partition files and does not decode shape data.
"""
import argparse
from collections import Counter
import importlib.util
import json
from pathlib import Path
import re
import struct
import sys
import uuid
import zlib

RANGE_LOD = '10dd104c-2ac8-11d1-9b6b-0080c7bb5997'
LOD = '10dd102c-2ac8-11d1-9b6b-0080c7bb5997'
SABOT = '96603dd3-5a0f-40f5-97cb-1e9647cbd37e'
END = 'ffffffff-ffff-ffff-ffff-ffffffffffff'
LIMIT = 512 * 1024 * 1024


def read_inventory(path):
    report = {'tool_schema': 1, 'geometry_exported': False, 'model_strings_exported': False, 'external_files_followed': False}
    with path.open('rb') as stream:
        size = stream.seek(0, 2)
        stream.seek(0)
        header = stream.read(109)
        match = re.match(rb'Version\s+(\d+)\.(\d+)', header[:80])
        if not match or len(header) < 105:
            raise ValueError('Not a complete JT header')
        major, minor = map(int, match.groups())
        if major < 8 or major > 10:
            raise ValueError('Header family is limited to 8.x/9.x/10.x')
        big = header[80] == 1
        endian = '>' if big else '<'
        report.update({'bytes': size, 'version': f'{major}.{minor}', 'byte_order': 'BE' if big else 'LE'})

        def guid(data):
            return str(uuid.UUID(bytes=data)) if big else str(uuid.UUID(bytes_le=data))

        def read_at(offset, count):
            if offset < 0 or count < 0 or offset + count > size:
                raise ValueError('Container range is outside the file')
            stream.seek(offset)
            data = stream.read(count)
            if len(data) != count:
                raise ValueError('Container read was truncated')
            return data

        offset_width = 8 if major >= 10 else 4
        toc = struct.unpack_from(endian + ('q' if major >= 10 else 'i'), header, 85)[0]
        lsg_id = guid(header[85 + offset_width:101 + offset_width])
        count = struct.unpack(endian + 'i', read_at(toc, 4))[0]
        width = 24 + offset_width
        if count < 1 or count > 1000000 or toc + 4 + count * width > size:
            raise ValueError('Invalid TOC count/range')
        table = read_at(toc + 4, count * width)
        segments, segment_bytes, lsg = Counter(), Counter(), None
        for index in range(count):
            entry = table[index * width:(index + 1) * width]
            offset, length, attributes = struct.unpack_from(endian + ('qiI' if major >= 10 else 'iiI'), entry, 16)
            if offset < 0 or length < 24 or offset + length > size:
                raise ValueError('Invalid TOC entry')
            kind = (attributes >> 24) & 255
            segments[kind] += 1
            segment_bytes[kind] += length
            if guid(entry[:16]) == lsg_id:
                lsg = (offset, length)
        report['shape_segments'] = {str(k): {'count': segments[k], 'bytes': segment_bytes[k]} for k in sorted(segments) if 6 <= k <= 16}
        if lsg is None:
            report['range_lod'] = 'root scene graph segment absent from the table of contents'
            return report
        seg = read_at(lsg[0], 33)
        kind, length, flag, encoded, algorithm = struct.unpack_from(endian + 'iiiiB', seg, 16)
        if kind != 1 or encoded < 1 or encoded > length - 32 or encoded - 1 > LIMIT:
            report['range_lod'] = 'not scanned: unexpected root scene graph header'
            return report
        packed = read_at(lsg[0] + 33, encoded - 1)
        if flag == 0:
            data = packed
        elif flag == 2 and algorithm == 2:
            decoder = zlib.decompressobj()
            data = decoder.decompress(packed, LIMIT + 1)
            if len(data) > LIMIT or not decoder.eof:
                report['range_lod'] = 'not scanned: decompressed size limit or truncated payload'
                return report
        else:
            report['range_lod'] = 'not scanned: root scene graph compression profile is not decoded by this tool'
            return report

        pos, section = 0, 0
        element_counts, limit_sets, centres = Counter(), Counter(), Counter()
        while section < 2:
            if pos + 4 > len(data):
                raise ValueError('Truncated graph element length')
            length = struct.unpack_from(endian + 'i', data, pos)[0]
            start, end = pos + 4, pos + 4 + length
            if length < 16 or end > len(data):
                raise ValueError('Invalid graph element envelope')
            pos = end
            value = guid(data[start:start + 16])
            if value == END:
                section += 1
                continue
            if section != 0 or length < 21:
                continue
            if value == SABOT:
                report['sabot_wrappers_present'] = True
                continue
            if value == LOD:
                element_counts['LOD'] += 1
            elif value == RANGE_LOD:
                element_counts['RangeLOD'] += 1
                if length < 21 + 4 + 12:
                    limit_sets['unparsed'] += 1
                    continue
                centre = struct.unpack_from(endian + '3f', data, end - 12)
                centres[tuple(round(x, 3) for x in centre)] += 1
                # Range Limits is a VecF32: I32 count then the floats, just before the Centre.
                for k in range(0, 16):
                    p = end - 12 - 4 * k - 4
                    if p > start + 21 and struct.unpack_from(endian + 'i', data, p)[0] == k:
                        limit_sets[str(tuple(round(x, 3) for x in struct.unpack_from(endian + f'{k}f', data, p + 4)))] += 1
                        break
                else:
                    limit_sets['unparsed'] += 1
        report['range_lod'] = {
            'elements': dict(element_counts),
            'range_limits': dict(limit_sets.most_common(8)),
            'centres': {str(k): v for k, v in centres.most_common(4)},
            'note': 'limits/centre are read from the end of the element payload; an empty tuple means no range limits',
        }
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('input', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        report = read_inventory(args.input)
    except (ValueError, OSError, zlib.error, struct.error) as error:
        print(f'error: {error}', file=sys.stderr)
        return 1
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=1), encoding='utf-8')
    return 0


if __name__ == '__main__':
    sys.exit(main())
