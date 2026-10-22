#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Author unsigned-byte JT pixel-format controls and glTF references from GTEX01.
Assets stay under ignored out/. Visual controls are opaque; alpha policy is covered
by prepared portable checks and the earlier separate transparency gates.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import shutil
from PIL import Image
from make_texture_control import assembly


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source_folder', type=Path); parser.add_argument('output_folder', type=Path)
    args = parser.parse_args(); source, output = args.source_folder.resolve(), args.output_folder.resolve()
    if not output.is_relative_to(Path(__file__).resolve().parents[2]/'out') or output == source:
        raise ValueError('Use a separate output folder inside ignored repository out/')
    doc = json.loads((source/'reference.gltf').read_text(encoding='utf-8'))
    image = Image.open(source/'source_image.png').convert('RGB')
    # Odd row width deliberately exercises alignment padding for all formats.
    width = image.width-1 if image.width%2 == 0 else image.width
    if width < 1:raise ValueError('Control source must be wider than one pixel')
    image = image.crop((0, 0, width, image.height)); output.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source/'geometry.jt', output/'geometry.jt')
    if (source/'SOURCE_LICENSE.md').exists():shutil.copyfile(source/'SOURCE_LICENSE.md', output/'SOURCE_LICENSE.md')
    for i, item in enumerate(doc['buffers']):
        name = f'source_buffer_{i}.bin'; shutil.copyfile(source/item['uri'], output/name); item['uri'] = name
    sampler = {'wrapS': 10497, 'wrapT': 10497, 'magFilter': 9729, 'minFilter': 9987}
    primitive = doc['meshes'][0]['primitives'][0]
    texture = doc['materials'][primitive['material']]['pbrMetallicRoughness']['baseColorTexture']['index']
    doc.setdefault('samplers', []).append(sampler); doc['textures'][texture]['sampler'] = len(doc['samplers'])-1
    authored = []
    for name, fmt in [('rgb',1),('rgba',2),('luminance',3),('luminance_alpha',4),('red',7),('green',8),('blue',9),('alpha',10),('bgr',11),('bgra',12)]:
        expected = Image.new('RGBA', image.size); encoded_rows = []
        for y in range(image.height):
            row = bytearray()
            for x in range(width):
                r, g, b = image.getpixel((x,y)); a = 255
                if fmt == 1:sample, color = (r,g,b), (r,g,b,a)
                elif fmt == 2:sample, color = (r,g,b,a), (r,g,b,a)
                elif fmt == 3:sample, color = (r,), (r,r,r,a)
                elif fmt == 4:sample, color = (r,a), (r,r,r,a)
                elif fmt == 7:sample, color = (r,), (r,0,0,a)
                elif fmt == 8:sample, color = (g,), (0,g,0,a)
                elif fmt == 9:sample, color = (b,), (0,0,b,a)
                elif fmt == 10:sample, color = (a,), (255,255,255,a)
                elif fmt == 11:sample, color = (b,g,r), (r,g,b,a)
                else:sample, color = (b,g,r,a), (r,g,b,a)
                row.extend(sample); expected.putpixel((x,y), color)
            row.extend([0xea]*((-len(row))%8)); encoded_rows.append(bytes(row))
        payload = b''.join(reversed(encoded_rows)); image_name = f'{name}_image.png'; expected.save(output/image_name)
        for big in (False, True):
            filename = f'{name}_{"be" if big else "le"}.jt'
            data = assembly(big, True, payload, width, image.height, sampler, pixel_format=fmt, alignment=8)
            (output/filename).write_bytes(data); authored.append({'file':filename,'sha256':hashlib.sha256(data).hexdigest()})
        reference = copy.deepcopy(doc); reference['images'][reference['textures'][texture]['source']]['uri'] = image_name
        (output/f'{name}_reference.gltf').write_text(json.dumps(reference, indent=2)+'\n', encoding='utf-8')
    report = {'source_folder':str(source),'image':[width,image.height],'alignment':8,'alpha':'opaque visual controls; no transparency acceptance inferred',
              'controls':authored,'method':'Original pixel packing and expected RGBA images; synthetic 10.0 attributes/8.2 geometry; no importer execution/vendor conformance'}
    (output/'authoring.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(f'Authored {len(authored)} pixel-format JT controls and ten glTF references in {output}')


if __name__ == '__main__':main()
