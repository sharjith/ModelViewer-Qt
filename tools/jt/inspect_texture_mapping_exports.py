#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Read-only effective UV inspection of original, vertex-order-preserving exports."""
import argparse
import json
import math
from pathlib import Path
import struct

from inspect_texture_exports import read_document, resource


def coordinates(path):
    document, binary = read_document(path)
    primitive = document['meshes'][0]['primitives'][0]
    material = document['materials'][primitive['material']]
    texture = material.get('pbrMetallicRoughness', {}).get('baseColorTexture')
    if texture is None: texture = material.get('normalTexture')
    if texture is None: raise ValueError('No colour or normal texture coordinate binding')
    channel = texture.get('texCoord', 0)
    accessor = document['accessors'][primitive['attributes']['TEXCOORD_'+str(channel)]]
    view = document['bufferViews'][accessor['bufferView']]
    buffer = document['buffers'][view.get('buffer', 0)]
    payload = resource(buffer['uri'], path) if 'uri' in buffer else binary
    if accessor['componentType'] != 5126 or accessor['type'] != 'VEC2' or 'sparse' in accessor:
        raise ValueError('Expected original dense float2 UVs')
    offset = view.get('byteOffset', 0)+accessor.get('byteOffset', 0)
    values = [struct.unpack_from('<ff', payload, offset+i*view.get('byteStride', 8))
              for i in range(accessor['count'])]
    if not values or not all(math.isfinite(v) for pair in values for v in pair):
        raise ValueError('Empty or nonfinite effective UVs')
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('checklist', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    if not args.output.resolve().is_relative_to(Path('out').resolve()):
        raise ValueError('Evidence must stay under ignored out/')
    results = []
    for case in json.loads(args.checklist.read_text())['cases']:
        expected = coordinates(Path(case['reference']))
        for fmt, path in case['exports'].items():
            row = dict(case=case['id'], format=fmt, file=path)
            try:
                actual = coordinates(Path(path))
                if len(actual) != len(expected): raise ValueError('Control vertex order/count changed')
                delta = max(abs(a-b) for av, bv in zip(actual, expected) for a, b in zip(av, bv))
                # These original controls quantize geometry/UV components to 16
                # bits. This tolerance is not a general exporter fidelity claim.
                row.update(max_uv_delta=delta, passed=delta < 5e-5)
            except Exception as error:
                row.update(passed=False, error=str(error))
            results.append(row)
    args.output.write_text(json.dumps(results, indent=2)+'\n')
    passed = sum(row['passed'] for row in results)
    print(f'{passed}/{len(results)} original effective-UV comparisons within quantization tolerance')
    raise SystemExit(0 if passed == len(results) else 1)


if __name__ == '__main__': main()
