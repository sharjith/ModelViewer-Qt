#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Author original border colour-format controls using the common asset store."""
import argparse
import copy
import json
import struct
from pathlib import Path
from PIL import Image
from make_texture_control import assembly
from make_jt9_texture_controls import assembly9
from texture_fixture_store import TextureFixtureStore


def convert(rgba,fmt):
    r,g,b,a=rgba
    return {
        1:((r,g,b),(r,g,b,255)),2:((r,g,b,a),(r,g,b,a)),
        3:((r,),(r,r,r,255)),4:((r,a),(r,r,r,a)),
        7:((r,),(r,0,0,255)),8:((g,),(0,g,0,255)),9:((b,),(0,0,b,255)),
        10:((a,),(255,255,255,a)),11:((b,g,r),(r,g,b,255)),12:((b,g,r,a),(r,g,b,a))}[fmt]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('source_folder','transform_folder','output_folder'):parser.add_argument(name,type=Path)
    args=parser.parse_args();source=args.source_folder.resolve();transformed=args.transform_folder.resolve()
    if args.output_folder.resolve() in (source,transformed):raise ValueError('Use a separate ignored output folder')
    store=TextureFixtureStore(args.output_folder);geometry=store.file(source/'geometry.jt')
    if (source/'SOURCE_LICENSE.md').exists():store.file(source/'SOURCE_LICENSE.md')
    image=Image.open(source/'source_image.png').convert('RGB');external=store.image(image)
    ring=Image.new('RGBA',(image.width+2,image.height+2),(255,255,255,255));ring.paste(image,(1,1))
    for x in range(1,ring.width-1):ring.putpixel((x,0),(255,0,255,255));ring.putpixel((x,ring.height-1),(0,255,255,255))
    for y in range(1,ring.height-1):ring.putpixel((0,y),(255,128,0,255));ring.putpixel((ring.width-1,y),(128,0,255,255))
    original=json.loads((transformed/'tile_2x2_reference.gltf').read_text(encoding='utf-8'))
    primitive=original['meshes'][0]['primitives'][0];accessor=original['accessors'][primitive['attributes']['TEXCOORD_0']]
    view=original['bufferViews'][accessor['bufferView']]
    if accessor['componentType']!=5126 or accessor['type']!='VEC2':raise ValueError('Expected original float2 UVs')
    data=(transformed/original['buffers'][view['buffer']]['uri']).read_bytes();offset=view.get('byteOffset',0)+accessor.get('byteOffset',0)
    uvs=[struct.unpack_from('<ff',data,offset+i*view.get('byteStride',8)) for i in range(accessor['count'])]
    template=copy.deepcopy(original)
    for field in ('buffers','images'):
        for item in template.get(field,[]):
            uri=item.get('uri')
            if uri and not uri.startswith('data:'):item['uri']=store.file(transformed/uri)
    effective=b''.join(struct.pack('<ff',(u*image.width+1)/ring.width,(v*image.height+1)/ring.height) for u,v in uvs)
    template['buffers'].append({'uri':store.asset(effective,'.bin'),'byteLength':len(effective)})
    template['bufferViews'].append({'buffer':len(template['buffers'])-1,'byteLength':len(effective)})
    template['accessors'].append({'bufferView':len(template['bufferViews'])-1,'componentType':5126,'count':len(uvs),'type':'VEC2'})
    template['meshes'][0]['primitives'][0]['attributes']['TEXCOORD_0']=len(template['accessors'])-1
    texture=template['materials'][primitive['material']]['pbrMetallicRoughness']['baseColorTexture']['index']
    template.setdefault('samplers',[]).append({'wrapS':33071,'wrapT':33071,'magFilter':9729,'minFilter':9729})
    template['textures'][texture]['sampler']=len(template['samplers'])-1
    def reference(label,expected):
        doc=copy.deepcopy(template);doc['images'][doc['textures'][texture]['source']]['uri']=store.image(expected);store.reference(doc,label)
    matrix=[float(i%5==0) for i in range(16)];matrix[0]=matrix[5]=2
    sampler={'wrapS':33069,'wrapT':33069,'magFilter':9729,'minFilter':9729};authored=[]
    for name,fmt in [('rgb',1),('rgba',2),('luminance',3),('luminance_alpha',4),('red',7),('green',8),('blue',9),('alpha',10),('bgr',11),('bgra',12)]:
        expected=Image.new('RGBA',ring.size);rows=[]
        for y in range(ring.height):
            row=bytearray()
            for x in range(ring.width):
                sample,color=convert(ring.getpixel((x,y)),fmt);row.extend(sample);expected.putpixel((x,y),color)
            rows.append(bytes(row))
        payload=b''.join(reversed(rows));reference(name,expected)
        for big in (False,True):
            endian='be' if big else 'le'
            authored.append(store.control(f'{name}_10_0_{endian}.jt',assembly(big,True,payload,ring.width,ring.height,sampler,matrix,pixel_format=fmt,geometry_reference=geometry,border_mode=2,image_border=1)))
            for minor,version in ((0,1),(5,2),(5,3)):
                authored.append(store.control(f'{name}_9_{minor}_v{version}_{endian}.jt',assembly9(minor,big,True,payload,ring.width,ring.height,matrix,[0]*4,[0.0]*16,local_version=version,coordinate=0,geometry_reference=geometry,border_mode=2,image_border=1,wrap_s=5,wrap_t=5,pixel_format=fmt,min_filter=2)))
    expected=Image.new('RGBA',ring.size,(51,102,153,255));expected.paste(image,(1,1));reference('constant_rgb',expected)
    for fmt,name in ((1,'rgb'),(11,'bgr')):
        payload=image.transpose(Image.Transpose.FLIP_TOP_BOTTOM).tobytes('raw','RGB' if fmt==1 else 'BGR')
        for big in (False,True):
            for inline in (False,True):
                label=f'{name}_constant_{"inline" if inline else "external"}_{"be" if big else "le"}.jt'
                authored.append(store.control(label,assembly(big,inline,payload,image.width,image.height,sampler,matrix,pixel_format=fmt,geometry_reference=geometry,image_reference=external,border_mode=1,border_color=(.2,.4,.6,0))))
    store.report({'controls':authored,'method':'Original colour-format border packing and canonical expected images, common geometry and independent padded UVs; synthetic only, no importer execution or exporter conformance'})
    print(f'Authored {len(authored)} positive colour-border roots; references and resources are shared in {store.root}')


if __name__=='__main__':main()
