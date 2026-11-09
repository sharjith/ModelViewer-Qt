#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Author no-UV planar JT controls and glTF references from the existing GTEX01 folder.
Source-derived assets stay under ignored out/. No JT reader or test is executed.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import shutil
import struct
from PIL import Image
from make_texture_control import assembly, geometry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source_folder', type=Path); parser.add_argument('output_folder', type=Path)
    args = parser.parse_args(); source, output = args.source_folder.resolve(), args.output_folder.resolve()
    if not output.is_relative_to(Path(__file__).resolve().parents[2]/'out') or output == source:
        raise ValueError('Use a separate output directory inside ignored repository out/')
    doc = json.loads((source/'reference.gltf').read_text(encoding='utf-8'))
    buffers = [(source/item['uri']).read_bytes() for item in doc['buffers']]
    primitive = doc['meshes'][0]['primitives'][0]
    def accessor(index):
        a = doc['accessors'][index]; view = doc['bufferViews'][a['bufferView']]
        code = {5121: 'B', 5123: 'H', 5125: 'I', 5126: 'f'}[a['componentType']]
        width = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3}[a['type']]; fmt = '<'+code*width
        offset = view.get('byteOffset', 0)+a.get('byteOffset', 0)
        return [struct.unpack_from(fmt, buffers[view['buffer']], offset+i*view.get('byteStride', struct.calcsize(fmt)))
                for i in range(a['count'])]
    positions = accessor(primitive['attributes']['POSITION'])
    indices = [item[0] for item in accessor(primitive['indices'])]
    image = Image.open(source/'source_image.png').convert('RGB')
    rgb = image.transpose(Image.Transpose.FLIP_TOP_BOTTOM).tobytes()
    output.mkdir(parents=True, exist_ok=True)
    # This companion deliberately has neither stored UV arrays nor a UV binding.
    data = geometry(positions, [(0, 0)]*len(positions), indices, store_uv=False)
    (output/'geometry.jt').write_bytes(data)
    image.save(output/'source_image.png')
    if (source/'SOURCE_LICENSE.md').exists():shutil.copyfile(source/'SOURCE_LICENSE.md', output/'SOURCE_LICENSE.md')
    for i, data in enumerate(buffers):
        (output/f'source_buffer_{i}.bin').write_bytes(data); doc['buffers'][i]['uri'] = f'source_buffer_{i}.bin'
    sampler = {'wrapS': 10497, 'wrapT': 10497, 'magFilter': 9729, 'minFilter': 9987}
    texture = doc['materials'][primitive['material']]['pbrMetallicRoughness']['baseColorTexture']['index']
    doc.setdefault('samplers', []).append(sampler); doc['textures'][texture]['sampler'] = len(doc['samplers'])-1
    doc['images'][doc['textures'][texture]['source']]['uri'] = 'source_image.png'
    authored = []
    for name in ('planar_xy', 'planar_yx', 'planar_offset', 'planar_tiled'):
        planes = [0.0]*16; planes[0] = planes[5] = 1
        transform = [float(i % 5 == 0) for i in range(16)]
        if name == 'planar_yx':planes[0] = planes[5] = 0; planes[1] = planes[4] = 1
        if name == 'planar_offset':planes[3], planes[7] = 0.25, -0.125
        if name == 'planar_tiled':transform[0] = transform[5] = 2
        for big in (False, True):
            for inline in (False, True):
                filename = f'{name}_{"inline" if inline else "external"}_{"be" if big else "le"}.jt'
                data = assembly(big, inline, rgb, image.width, image.height, sampler, transform, [1, 1, 0, 0], planes)
                (output/filename).write_bytes(data); authored.append({'file': filename, 'sha256': hashlib.sha256(data).hexdigest()})
        # Independent image-space expectations, not computed by the JT reader.
        if name == 'planar_yx':effective = [(y, 1-x) for x, y, z in positions]
        elif name == 'planar_offset':effective = [(x+0.25, 1.125-y) for x, y, z in positions]
        elif name == 'planar_tiled':effective = [(2*x, 1-2*y) for x, y, z in positions]
        else:effective = [(x, 1-y) for x, y, z in positions]
        data = b''.join(struct.pack('<ff', *uv) for uv in effective); filename = f'{name}_uv.bin'
        (output/filename).write_bytes(data); reference = copy.deepcopy(doc)
        reference['buffers'].append({'uri': filename, 'byteLength': len(data)})
        reference['bufferViews'].append({'buffer': len(reference['buffers'])-1, 'byteLength': len(data)})
        reference['accessors'].append({'bufferView': len(reference['bufferViews'])-1, 'componentType': 5126, 'count': len(positions), 'type': 'VEC2'})
        reference['meshes'][0]['primitives'][0]['attributes']['TEXCOORD_0'] = len(reference['accessors'])-1
        (output/f'{name}_reference.gltf').write_text(json.dumps(reference, indent=2)+'\n', encoding='utf-8')
    report = {'source_folder': str(source), 'geometry_sha256': hashlib.sha256((output/'geometry.jt').read_bytes()).hexdigest(),
              'stored_uv': False, 'vertices': len(positions), 'triangles': len(indices)//3, 'controls': authored,
              'method': 'Synthetic modern model-linear planes and 8.2 geometry without UVs; separately authored reference UVs; no importer execution/vendor-export qualification'}
    (output/'authoring.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(f'Authored {len(authored)} planar JT controls and four glTF references in {output}')


if __name__ == '__main__':main()
