#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Author bounded RGBA constant-border controls and independent padded glTFs."""
import argparse
import copy
import json
import struct
from pathlib import Path
from PIL import Image
from make_texture_control import assembly
from make_jt9_texture_controls import assembly9
from texture_fixture_store import TextureFixtureStore


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source_folder',type=Path)
    parser.add_argument('transform_folder',type=Path)
    parser.add_argument('output_folder',type=Path)
    args=parser.parse_args();source=args.source_folder.resolve();transformed=args.transform_folder.resolve()
    output=args.output_folder.resolve()
    if output in (source,transformed):raise ValueError('Use a separate ignored output folder')
    store=TextureFixtureStore(output);geometry=store.file(source/'geometry.jt')
    if (source/'SOURCE_LICENSE.md').exists():store.file(source/'SOURCE_LICENSE.md')
    image=Image.open(source/'source_image.png').convert('RGBA');image_ref=store.image(image)
    payload=image.transpose(Image.Transpose.FLIP_TOP_BOTTOM).tobytes()
    original=json.loads((transformed/'tile_2x2_reference.gltf').read_text(encoding='utf-8'))
    primitive=original['meshes'][0]['primitives'][0]
    accessor=original['accessors'][primitive['attributes']['TEXCOORD_0']]
    view=original['bufferViews'][accessor['bufferView']]
    if accessor['componentType']!=5126 or accessor['type']!='VEC2':raise ValueError('Expected original float2 UVs')
    uvdata=(transformed/original['buffers'][view['buffer']]['uri']).read_bytes()
    offset=view.get('byteOffset',0)+accessor.get('byteOffset',0)
    uvs=[struct.unpack_from('<ff',uvdata,offset+i*view.get('byteStride',8)) for i in range(accessor['count'])]
    matrix=[float(i%5==0) for i in range(16)];matrix[0]=matrix[5]=2
    authored=[]
    for label,s,t,other,color in [('both',1,1,10497,(1,.5,0,1)),('s_repeat',1,0,10497,(1,.5,0,1)),('t_mirror',0,1,33648,(0,1,1,1))]:
        width=image.width+2*s;height=image.height+2*t
        padded=Image.new('RGBA',(width,height),tuple(round(v*255) for v in color));padded.paste(image,(s,t))
        reference=copy.deepcopy(original)
        # glTF's top-origin V and JT's bottom-origin T share this symmetric
        # interior-to-padded-image formula; no model up-axis rotation is needed.
        effective=b''.join(struct.pack('<ff',(u*image.width+s)/width,(v*image.height+t)/height) for u,v in uvs)
        reference['buffers'].append({'uri':store.asset(effective,'.bin'),'byteLength':len(effective)})
        reference['bufferViews'].append({'buffer':len(reference['buffers'])-1,'byteLength':len(effective)})
        reference['accessors'].append({'bufferView':len(reference['bufferViews'])-1,'componentType':5126,'count':len(uvs),'type':'VEC2'})
        reference['meshes'][0]['primitives'][0]['attributes']['TEXCOORD_0']=len(reference['accessors'])-1
        texture=reference['materials'][primitive['material']]['pbrMetallicRoughness']['baseColorTexture']['index']
        sampler={'wrapS':33071 if s else other,'wrapT':33071 if t else other,'magFilter':9729,'minFilter':9729}
        reference.setdefault('samplers',[]).append(sampler);reference['textures'][texture]['sampler']=len(reference['samplers'])-1
        # Resolve existing source resources before appending shared resources.
        for field in ('buffers','images'):
            for item in reference.get(field,[]):
                uri=item.get('uri')
                if uri and not uri.startswith(('data:','assets/')):item['uri']=store.file(transformed/uri)
        reference['images'][reference['textures'][texture]['source']]['uri']=store.image(padded)
        store.reference(reference,label)
        jt_sampler=dict(sampler,wrapS=33069 if s else other,wrapT=33069 if t else other)
        for big in (False,True):
            endian='be' if big else 'le'
            for inline in (False,True):
                storage='inline' if inline else 'external'
                data=assembly(big,inline,payload,image.width,image.height,jt_sampler,matrix,pixel_format=2,
                    geometry_reference=geometry,image_reference=image_ref,border_mode=1,border_color=color)
                authored.append(store.control(f'10_0_{label}_{storage}_{endian}.jt',data))
            for minor in (0,5):
                for version in (1,2,3):
                    data=assembly9(minor,big,True,payload,image.width,image.height,matrix,[0]*4,[0.0]*16,
                        local_version=version,coordinate=0,geometry_reference=geometry,border_mode=1,
                        wrap_s=5 if s else (2 if other==10497 else 3),wrap_t=5 if t else (2 if other==10497 else 3),
                        pixel_format=2,min_filter=2,border_color=color)
                    authored.append(store.control(f'9_{minor}_v{version}_{label}_{endian}.jt',data))
    store.report({'controls':authored,'method':'Original RGBA nearest/linear constant-border envelopes, shared geometry, independently padded glTF image/UV oracle; synthetic, no importer execution or exporter conformance'})
    print(f'Authored {len(authored)} positive constant-border roots and three shared references in {store.root}')


if __name__=='__main__':main()
