#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Read-only GLB alpha inventory; neutral JSON evidence stays under ignored out/."""
import argparse
import hashlib
import io
import json
import struct
from pathlib import Path
from PIL import Image


def inspect(path):
    if path.stat().st_size>512*1024*1024:raise ValueError('GLB exceeds analysis byte limit')
    raw=path.read_bytes()
    if len(raw)<20 or raw[:4]!=b'glTF':raise ValueError('Expected GLB')
    version,total=struct.unpack_from('<II',raw,4)
    if version!=2 or total!=len(raw):raise ValueError('Invalid GLB version/size')
    chunks={};offset=12
    while offset<len(raw):
        size,kind=struct.unpack_from('<II',raw,offset);offset+=8
        if offset+size>len(raw):raise ValueError('Truncated GLB chunk')
        chunks[kind]=(offset,size);offset+=size
    begin,size=chunks[0x4e4f534a];document=json.loads(raw[begin:begin+size])
    binary_start,binary_size=chunks.get(0x004e4942,(0,0))
    materials=[]
    for index,material in enumerate(document.get('materials',[])):
        pbr=material.get('pbrMetallicRoughness',{});record={'id':index,'alpha_mode':material.get('alphaMode','OPAQUE'),
            'factor_alpha':pbr.get('baseColorFactor',[1,1,1,1])[3]}
        base=pbr.get('baseColorTexture')
        if base:
            source=document['textures'][base['index']].get('source')
            record['image_index']=source
            image=document['images'][source] if source is not None else {}
            if 'bufferView' in image:
                view=document['bufferViews'][image['bufferView']];start=view.get('byteOffset',0);size=view['byteLength']
                if view.get('buffer',0)!=0 or start+size>binary_size:raise ValueError('Invalid image view')
                decoded=Image.open(io.BytesIO(raw[binary_start+start:binary_start+start+size]))
                if decoded.width*decoded.height>16*1024*1024:raise ValueError('Image exceeds analysis pixel limit')
                alpha=decoded.convert('RGBA').getchannel('A');histogram=alpha.histogram()
                record.update(dimensions=list(decoded.size),alpha_range=list(alpha.getextrema()),
                    opaque_texels=histogram[255],zero_texels=histogram[0],partial_texels=sum(histogram[1:255]))
            else:record['image_analysis']='External/non-core image source not decoded'
        materials.append(record)
    return {'source_sha256':hashlib.sha256(raw).hexdigest(),'material_count':len(materials),
        'vertex_color_primitive_count':sum('COLOR_0' in primitive.get('attributes',{}) for mesh in document.get('meshes',[]) for primitive in mesh.get('primitives',[])),
        'materials':materials,'limits':'Source alpha inventory only; no renderer execution or per-fragment UV coverage qualification'}


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('input',type=Path);parser.add_argument('output',type=Path);args=parser.parse_args()
    root=Path(__file__).resolve().parents[2]/'out'
    if not args.output.resolve().is_relative_to(root):raise ValueError('Evidence must stay under ignored out/')
    result=inspect(args.input);args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(f"Inventoried {result['material_count']} materials; vertex-colour primitives: {result['vertex_color_primitive_count']}")


if __name__=='__main__':main()
