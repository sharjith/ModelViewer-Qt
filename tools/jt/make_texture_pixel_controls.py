#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Author unsigned 8/16/32-bit or float16/32 JT pixel controls and glTF references from GTEX01.
Assets stay under ignored out/. Visual controls are opaque; alpha policy is covered
by prepared portable checks and the earlier separate transparency gates.
"""
import argparse
import copy
import json
from pathlib import Path
import struct
from PIL import Image
from make_texture_control import assembly
from texture_fixture_store import TextureFixtureStore


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source_folder', type=Path); parser.add_argument('output_folder', type=Path)
    numeric=parser.add_mutually_exclusive_group()
    numeric.add_argument('--unsigned16',action='store_true',help='Pack unsigned 16-bit components with distinguishable bytes')
    numeric.add_argument('--unsigned32',action='store_true',help='Pack unsigned 32-bit components with distinguishable bytes')
    numeric.add_argument('--float32',action='store_true',help='Pack normalized float32 colour components')
    numeric.add_argument('--float16',action='store_true',help='Pack normalized IEEE binary16 colour components')
    args = parser.parse_args(); source, output = args.source_folder.resolve(), args.output_folder.resolve()
    if not output.is_relative_to(Path(__file__).resolve().parents[2]/'out') or output == source:
        raise ValueError('Use a separate output folder inside ignored repository out/')
    doc = json.loads((source/'reference.gltf').read_text(encoding='utf-8'))
    image = Image.open(source/'source_image.png').convert('RGB')
    # Odd row width deliberately exercises alignment padding for all formats.
    width = image.width-1 if image.width%2 == 0 else image.width
    if width < 1:raise ValueError('Control source must be wider than one pixel')
    image = image.crop((0, 0, width, image.height)); store=TextureFixtureStore(output)
    geometry_reference=store.file(source/'geometry.jt')
    obsolete=[output/'geometry.jt',output/'SOURCE_LICENSE.md']
    if (source/'SOURCE_LICENSE.md').exists():store.file(source/'SOURCE_LICENSE.md')
    for field in ('buffers','images'):
        for i,item in enumerate(doc.get(field,[])):
            uri=item.get('uri')
            if uri and not uri.startswith('data:'):item['uri']=store.file(source/uri)
            if field=='buffers':obsolete.append(output/f'source_buffer_{i}.bin')
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
                if args.float32 or args.float16:
                    row.extend(struct.pack('<'+('e' if args.float16 else 'f')*len(sample),*(value/255.0 for value in sample)))
                elif args.unsigned32:
                    for value in sample:
                        word=0 if value==0 else 0xffffffff if value==255 else value*0x01010101+0x1234
                        row.extend(word.to_bytes(4,'little'))
                elif args.unsigned16:
                    for value in sample:
                        word=0 if value==0 else 65535 if value==255 else value*257+73
                        row.extend(word.to_bytes(2,'little'))
                else:row.extend(sample)
                expected.putpixel((x,y), color)
            row.extend([0xea]*((-len(row))%8)); encoded_rows.append(bytes(row))
        payload = b''.join(reversed(encoded_rows)); image_name=store.image(expected)
        obsolete.extend([output/f'{name}_image.png',output/f'{name}_reference.gltf'])
        for big in (False, True):
            filename = f'{name}_{"be" if big else "le"}.jt'
            packed=payload
            if args.unsigned16 and big:
                packed=bytearray(payload);packed[0::2],packed[1::2]=payload[1::2],payload[0::2]
            if (args.float32 or args.float16 or args.unsigned32) and big:
                size=2 if args.float16 else 4
                packed=b''.join(payload[i:i+size][::-1] for i in range(0,len(payload),size))
            data = assembly(big, True, packed, width, image.height, sampler, pixel_format=fmt, alignment=8,pixel_type=9 if args.float16 else 2 if args.float32 else 7 if args.unsigned32 else 5 if args.unsigned16 else 3,geometry_reference=geometry_reference)
            authored.append(store.control(filename,data));obsolete.append(output/filename)
        reference = copy.deepcopy(doc); reference['images'][reference['textures'][texture]['source']]['uri'] = image_name
        store.reference(reference,name)
    report = {'source_folder':str(source),'image':[width,image.height],'alignment':8,'pixel_type':9 if args.float16 else 2 if args.float32 else 7 if args.unsigned32 else 5 if args.unsigned16 else 3,'alpha':'opaque visual controls; no transparency acceptance inferred',
              'controls':authored,'method':'Original pixel packing and expected RGBA images; synthetic 10.0 attributes/8.2 geometry; no importer execution/vendor conformance'}
    store.report(report);store.cleanup(obsolete)
    print(f'Authored {len(authored)} controls in {store.root}; gate manifest: {output}/authoring.json')


if __name__ == '__main__':main()
