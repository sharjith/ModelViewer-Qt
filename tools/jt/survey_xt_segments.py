#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Read-only survey of the Parasolid (XT) B-Rep segments inside JT files.

For every JT file under the given paths it lists the XT segments (TOC segment type 17 or 30) and reports, per file,
the modeller version and internal schema key written in each XT stream, and whether the key carries an embedded
schema (SCH_<n>_<n>_<base>) or names an external one (SCH_<n>_<n>). No geometry is exported; sample file names are
hashed so the output can be quoted with neutral IDs.

    python tools/jt/survey_xt_segments.py <file-or-folder>... --output out/jt-research/xt-survey.tsv

Uses only the standard library. Segment payloads are searched for the neutral-binary flag sequence 'PS\\0\\0',
directly and after zlib or LZMA (xz) decompression, because the compression framing differs between JT versions.
"""
import argparse
import hashlib
from collections import Counter
from pathlib import Path
import re
import lzma
import struct
import uuid
import zlib

KEY = re.compile(rb'SCH_(\d+)_(\d+)(?:_(\d+))?')
MODELLER = re.compile(rb'modeller version (\d+)')


def segments(data):
    m = re.match(rb'Version\s+(\d+)\.(\d+)', data[:80])
    if not m:
        raise ValueError('not a JT header')
    major = int(m.group(1))
    big = data[80] == 1
    e = '>' if big else '<'
    w = 8 if major >= 10 else 4
    toc = struct.unpack_from(e + ('q' if major >= 10 else 'i'), data, 85)[0]
    count = struct.unpack_from(e + 'i', data, toc)[0]
    width = 24 + w
    for i in range(count):
        base = toc + 4 + i * width
        offset, length, attrs = struct.unpack_from(e + ('qiI' if major >= 10 else 'iiI'), data, base + 16)
        yield (attrs >> 24) & 255, offset, length


def xt_streams(blob):
    """Yield the byte strings that start with the XT flag sequence, direct or inside a zlib stream."""
    at = blob.find(b'PS\x00\x00')
    if at >= 0:
        yield blob[at:]
        return
    for m in re.finditer(rb'\xfd7zXZ\x00', blob):
        try:
            out = lzma.LZMADecompressor().decompress(blob[m.start():])
        except lzma.LZMAError:
            continue
        at = out.find(b'PS\x00\x00')
        if at >= 0:
            yield out[at:]
            return
    for m in re.finditer(rb'\x78[\x01\x5e\x9c\xda]', blob):
        try:
            out = zlib.decompressobj().decompress(blob[m.start():])
        except zlib.error:
            continue
        at = out.find(b'PS\x00\x00')
        if at >= 0:
            yield out[at:]
            return


def survey(path, extract=None, sample=''):
    data = path.read_bytes()
    result = Counter()
    rows = []
    for stype, offset, length in segments(data):
        if stype not in (17, 30):
            continue
        for stream in xt_streams(data[offset:offset + length]):
            head = stream[:600]
            if extract is not None:
                extract.mkdir(parents=True, exist_ok=True)
                (extract / f'{sample}_{offset}.bin').write_bytes(stream)
            key = KEY.search(head)
            mod = MODELLER.search(head)
            if key:
                parts = [p for p in key.groups() if p is not None]
                kind = 'embedded' if len(parts) == 3 else 'external'
                result[(mod.group(1).decode() if mod else '?', key.group(0).decode(), kind)] += 1
            else:
                result[('?', 'no-key', 'unknown')] += 1
            break
        else:
            result[('?', 'no-xt-stream', 'unknown')] += 1
    return len(data), result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('inputs', nargs='+', type=Path)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--extract-dir', type=Path, help='also write each XT stream as <sample>_<segment offset>.bin (for parasolid_xt_shapes --jt-binary); keep under out/')
    a = p.parse_args()
    files = []
    for item in a.inputs:
        files += sorted(item.rglob('*.jt')) if item.is_dir() else [item]
    a.output.parent.mkdir(parents=True, exist_ok=True)
    with a.output.open('w', encoding='utf-8') as out:
        out.write('sample\tbytes\tmodeller\tschema_key\tkind\tsegments\n')
        for f in files:
            sample = 'JT-' + hashlib.sha256(f.name.encode('utf-8', 'replace')).hexdigest()[:8]
            try:
                size, result = survey(f, a.extract_dir, sample)
            except Exception as e:  # noqa: BLE001 - a survey keeps going
                out.write(f'{sample}\t0\t?\t{type(e).__name__}\terror\t0\n')
                continue
            if not result:
                out.write(f'{sample}\t{size}\t-\t-\tno-xt\t0\n')
            for (mod, key, kind), n in sorted(result.items()):
                out.write(f'{sample}\t{size}\t{mod}\t{key}\t{kind}\t{n}\n')
    print('files:', len(files), '->', a.output)


if __name__ == '__main__':
    main()
