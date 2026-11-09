#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Author affine JT/glTF comparison controls from the existing GTEX01 fixture folder.
Outputs are source-derived assets and must remain inside ignored repository out/.
This serializes synthetic controls; it does not build/run the JT importer.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import shutil
import struct
from PIL import Image
from make_texture_control import assembly


def cases():
    identity = [float(i % 5 == 0) for i in range(16)]
    yield 'identity', identity.copy(), lambda u, v: (u, v)
    t = identity.copy(); t[0] = t[5] = 2
    yield 'tile_2x2', t, lambda u, v: (2*u, 2*v-1)
    t = identity.copy(); t[12], t[13] = 0.25, 0.125
    yield 'offset', t, lambda u, v: (u+0.25, v-0.125)
    t = identity.copy(); t[0], t[12] = -1, 1
    yield 'mirror_u', t, lambda u, v: (1-u, v)
    t = identity.copy(); t[0], t[1], t[4], t[5], t[12] = 0, 1, -1, 0, 1
    yield 'rotate_90', t, lambda u, v: (v, 1-u)
    t = identity.copy(); t[4] = 0.5
    yield 'shear', t, lambda u, v: (u+0.5*(1-v), v)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source_folder', type=Path); parser.add_argument('output_folder', type=Path)
    args = parser.parse_args(); source, output = args.source_folder.resolve(), args.output_folder.resolve()
    repo = Path(__file__).resolve().parents[2]
    if not output.is_relative_to(repo/'out') or output == source:
        raise ValueError('Use a separate output directory inside ignored repository out/')
    doc = json.loads((source/'reference.gltf').read_text(encoding='utf-8'))
    primitive = doc['meshes'][0]['primitives'][0]
    accessor = doc['accessors'][primitive['attributes']['TEXCOORD_0']]
    view = doc['bufferViews'][accessor['bufferView']]
    if accessor['type'] != 'VEC2' or accessor['componentType'] != 5126:
        raise ValueError('Expected the original float2 GTEX01 UV accessor')
    buffers = [(source/item['uri']).read_bytes() for item in doc['buffers']]
    offset = view.get('byteOffset', 0)+accessor.get('byteOffset', 0)
    uvs = [struct.unpack_from('<ff', buffers[view['buffer']], offset+i*view.get('byteStride', 8))
           for i in range(accessor['count'])]
    image = Image.open(source/'source_image.png').convert('RGB')
    rgb = image.transpose(Image.Transpose.FLIP_TOP_BOTTOM).tobytes()
    output.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source/'geometry.jt', output/'geometry.jt')
    image.save(output/'source_image.png')
    if (source/'SOURCE_LICENSE.md').exists():
        shutil.copyfile(source/'SOURCE_LICENSE.md', output/'SOURCE_LICENSE.md')
    for i, data in enumerate(buffers):
        (output/f'source_buffer_{i}.bin').write_bytes(data)
        doc['buffers'][i]['uri'] = f'source_buffer_{i}.bin'
    sampler = {'wrapS': 10497, 'wrapT': 10497, 'magFilter': 9729, 'minFilter': 9987}
    texture_index = doc['materials'][primitive['material']]['pbrMetallicRoughness']['baseColorTexture']['index']
    doc.setdefault('samplers', []).append(sampler)
    doc['textures'][texture_index]['sampler'] = len(doc['samplers'])-1
    doc['images'][doc['textures'][texture_index]['source']]['uri'] = 'source_image.png'
    authored = []
    for name, matrix, reference_uv in cases():
        for big in (False, True):
            for inline in (False, True):
                filename = f'{name}_{"inline" if inline else "external"}_{"be" if big else "le"}.jt'
                data = assembly(big, inline, rgb, image.width, image.height, sampler, matrix)
                (output/filename).write_bytes(data)
                authored.append({'file': filename, 'sha256': hashlib.sha256(data).hexdigest()})
        reference = copy.deepcopy(doc)
        effective = b''.join(struct.pack('<ff', *reference_uv(u, v)) for u, v in uvs)
        filename = f'{name}_uv.bin'; (output/filename).write_bytes(effective)
        reference['buffers'].append({'uri': filename, 'byteLength': len(effective)})
        reference['bufferViews'].append({'buffer': len(reference['buffers'])-1, 'byteLength': len(effective)})
        reference['accessors'].append({'bufferView': len(reference['bufferViews'])-1, 'componentType': 5126,
                                       'count': len(uvs), 'type': 'VEC2'})
        reference['meshes'][0]['primitives'][0]['attributes']['TEXCOORD_0'] = len(reference['accessors'])-1
        (output/f'{name}_reference.gltf').write_text(json.dumps(reference, indent=2)+'\n', encoding='utf-8')
    projective = [float(i % 5 == 0) for i in range(16)]; projective[3] = 0.5
    (output/'unsupported_projective.jt').write_bytes(assembly(False, True, rgb, image.width, image.height, sampler, projective))
    report = {'source_folder': str(source), 'method': 'Original affine matrices and separately authored glTF UV expressions; synthetic 10.0 texture / 8.2 geometry; no importer execution or vendor-export qualification',
              'controls': authored, 'reference_uv_vertices': len(uvs)}
    (output/'authoring.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(f'Authored {len(authored)} JT comparisons, six glTF references and one unsupported-projective control in {output}')


if __name__ == '__main__':
    main()
