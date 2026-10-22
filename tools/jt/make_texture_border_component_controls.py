#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Original nested cubes for luminance/alpha constant-border inspection."""
import argparse
import copy
import struct
from PIL import Image
from pathlib import Path
from make_texture_control import assembly,geometry
from make_jt9_texture_controls import assembly9
from texture_fixture_store import TextureFixtureStore


def cubes():
    faces=[[(1,-1,-1),(1,1,-1),(1,1,1),(1,-1,1)],
           [(-1,1,-1),(-1,-1,-1),(-1,-1,1),(-1,1,1)],
           [(1,1,-1),(-1,1,-1),(-1,1,1),(1,1,1)],
           [(-1,-1,-1),(1,-1,-1),(1,-1,1),(-1,-1,1)],
           [(-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)],
           [(-1,1,-1),(1,1,-1),(1,-1,-1),(-1,-1,-1)]]
    positions=[v for face in faces for v in face]
    normals=[n for n in [(1,0,0),(-1,0,0),(0,1,0),(0,-1,0),(0,0,1),(0,0,-1)] for _ in range(4)]
    uv=[v for _ in faces for v in [(0,0),(2,0),(2,2),(0,2)]]
    indices=[4*f+i for f in range(6) for i in [0,1,2,0,2,3]]
    return positions,normals,uv,indices


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('output_folder',type=Path);args=parser.parse_args()
    store=TextureFixtureStore(args.output_folder);positions,normals,uv,indices=cubes()
    inner=[tuple(.55*v for v in p) for p in positions]
    front=store.asset(geometry(positions,uv,indices,blend=True,diffuse=(0,0,1,1)),'.jt')
    back=store.asset(geometry(inner,uv,indices,diffuse=(1,0,0,1)),'.jt')
    data=bytearray();views=[];accessors=[]
    def accessor(values,fmt,kind,component):
        while len(data)%4:data.append(0)
        offset=len(data)
        for value in values:data.extend(struct.pack('<'+fmt,*(value if isinstance(value,tuple) else (value,))))
        views.append({'buffer':0,'byteOffset':offset,'byteLength':len(data)-offset})
        accessors.append({'bufferView':len(views)-1,'componentType':component,'count':len(values),'type':kind})
        if kind=='VEC3' and fmt=='fff':
            accessors[-1]['min']=[min(v[i] for v in values) for i in range(3)];accessors[-1]['max']=[max(v[i] for v in values) for i in range(3)]
        return len(accessors)-1
    frontpos=accessor(positions,'fff','VEC3',5126);backpos=accessor(inner,'fff','VEC3',5126)
    normal=accessor(normals,'fff','VEC3',5126)
    mapped=accessor([(u*.5+.25,(1-v)*.5+.25) for u,v in uv],'ff','VEC2',5126)
    index=accessor(indices,'H','SCALAR',5123);blob=store.asset(bytes(data),'.bin')
    template={'asset':{'version':'2.0','generator':'ModelViewer original constant-border nested-cube control'},
        'buffers':[{'uri':blob,'byteLength':len(data)}],'bufferViews':views,'accessors':accessors,
        'images':[{}],'samplers':[{'wrapS':33071,'wrapT':33071,'magFilter':9729,'minFilter':9729}],
        'textures':[{'source':0,'sampler':0}],
        'materials':[{'alphaMode':'BLEND','pbrMetallicRoughness':{'baseColorFactor':[0,0,1,1],'baseColorTexture':{'index':0},'metallicFactor':0,'roughnessFactor':1}},
                     {'pbrMetallicRoughness':{'baseColorFactor':[1,0,0,1],'metallicFactor':0,'roughnessFactor':1}}],
        'meshes':[{'primitives':[{'attributes':{'POSITION':frontpos,'NORMAL':normal,'TEXCOORD_0':mapped},'indices':index,'material':0}]},
                  {'primitives':[{'attributes':{'POSITION':backpos,'NORMAL':normal},'indices':index,'material':1}]}],
        'nodes':[{'mesh':0,'name':'Blue outer cube'},{'mesh':1,'name':'Red inner cube'}],'scenes':[{'nodes':[0,1]}],'scene':0}
    cases=[('luminance_opaque',3,0),('luminance_alpha_partial',4,.25),('alpha_partial',10,.25),('alpha_zero',10,0),('alpha_opaque',10,1)]
    sampler={'wrapS':33069,'wrapT':33069,'magFilter':9729,'minFilter':9729};identity=[float(i%5==0) for i in range(16)];authored=[]
    for label,fmt,alpha in cases:
        payload=bytes([204] if fmt==3 else [204,255] if fmt==4 else [255])*4
        border=(.2,.6,.9,alpha)
        expected=Image.new('RGBA',(4,4),(51,51,51,255) if fmt==3 else (51,51,51,64) if fmt==4 else (255,255,255,round(alpha*255)))
        expected.paste((204,204,204,255) if fmt in (3,4) else (255,255,255,255),(1,1,3,3))
        reference=copy.deepcopy(template);reference['images'][0]['uri']=store.image(expected)
        if fmt==3 or alpha==1:reference['materials'][0]['alphaMode']='OPAQUE'
        store.reference(reference,label)
        for big in (False,True):
            endian='be' if big else 'le'
            authored.append(store.control(f'{label}_10_0_{endian}.jt',assembly(big,True,payload,2,2,sampler,pixel_format=fmt,geometry_reference=front,back_reference=back,border_mode=1,border_color=border)))
            for minor,version in ((0,1),(5,2),(5,3)):
                authored.append(store.control(f'{label}_9_{minor}_v{version}_{endian}.jt',assembly9(minor,big,True,payload,2,2,identity,[0]*4,[0.0]*16,local_version=version,coordinate=0,geometry_reference=front,back_reference=back,pixel_format=fmt,border_mode=1,border_color=border,wrap_s=5,wrap_t=5,min_filter=2)))
    store.report({'controls':authored,'method':'Original blue outer/red inner cubes, independent glTF padded pixels/UVs, constant luminance/alpha profiles; synthetic only, no importer execution or exporter conformance'})
    print(f'Authored {len(authored)} positive nested-cube roots and five shared references in {store.root}')


if __name__=='__main__':main()
