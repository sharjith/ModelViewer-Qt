#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Read-only C2aa exported image/mip/alpha inspection. Evidence stays under out/."""
import argparse
import base64
import hashlib
import io
import json
import struct
from pathlib import Path
from PIL import Image

KEY='ModelViewer.MipChain.v1'
TERMINAL='ModelViewer.MipChain.Terminal.v1'
LIMIT=64*1024*1024


def read_document(path):
    data=path.read_bytes()
    if len(data)>LIMIT:raise ValueError('Document exceeds inspection limit')
    if path.suffix=='.glb':
        magic,version,size=struct.unpack_from('<III',data)
        if magic!=0x46546c67 or version!=2 or size!=len(data):raise ValueError('Invalid GLB header')
        length,kind=struct.unpack_from('<II',data,12)
        if kind!=0x4e4f534a:raise ValueError('Missing JSON chunk')
        document=json.loads(data[20:20+length]);offset=20+length
        binary=b''
        if offset<len(data):
            length,kind=struct.unpack_from('<II',data,offset)
            if kind!=0x004e4942 or offset+8+length!=len(data):raise ValueError('Invalid BIN chunk')
            binary=data[offset+8:offset+8+length]
        return document,binary
    return json.loads(data),b''


def resource(uri,path):
    if uri.startswith('data:'):return base64.b64decode(uri.split(',',1)[1],validate=True)
    if '://' in uri:raise ValueError('Unresolved virtual image/buffer URI')
    target=(path.parent/uri).resolve()
    if not target.is_relative_to(Path('out').resolve()):raise ValueError('Resource outside ignored out/')
    return target.read_bytes()


def appearance(path):
    document,binary=read_document(path)
    material=next(m for m in document['materials'] if 'baseColorTexture' in m.get('pbrMetallicRoughness',{}))
    factor=material['pbrMetallicRoughness'].get('baseColorFactor',[1,1,1,1])
    if factor!=[1,1,1,1]:raise ValueError('Composed image should have neutral factors')
    texture=document['textures'][material['pbrMetallicRoughness']['baseColorTexture']['index']]
    image=document['images'][texture['source']]
    if 'uri' in image:encoded=resource(image['uri'],path)
    else:
        view=document['bufferViews'][image['bufferView']];buffer=document['buffers'][view.get('buffer',0)]
        payload=resource(buffer['uri'],path) if 'uri' in buffer else binary
        offset=view.get('byteOffset',0);encoded=payload[offset:offset+view['byteLength']]
    if not encoded or len(encoded)>LIMIT:raise ValueError('Invalid encoded image size')
    sampler_index=texture.get('sampler',-1)
    if not 0<=sampler_index<len(document.get('samplers',[])):raise ValueError('Missing/invalid authored sampler')
    sampler=document['samplers'][sampler_index]
    base=Image.open(io.BytesIO(encoded))
    if base.width*base.height*4>LIMIT:raise ValueError('Decoded image exceeds limit')
    base.load()
    levels=[base]+[Image.open(io.BytesIO(base64.b64decode(s,validate=True))) for s in json.loads(base.info.get(KEY,'[]'))]
    signatures=[]
    for level in levels:
        if level.width*level.height*4>LIMIT:raise ValueError('Decoded image exceeds limit')
        signatures.append(dict(size=level.size,rgba_sha256=hashlib.sha256(level.convert('RGBA').tobytes()).hexdigest()))
    return dict(levels=signatures,terminal=base.info.get(TERMINAL,''),alpha=material.get('alphaMode','OPAQUE'),
                double_sided=material.get('doubleSided',False),
                material_sides=[m.get('doubleSided',False) for m in document['materials']],
                sampler={key:sampler.get(key) for key in ('wrapS','wrapT','magFilter','minFilter')})


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('checklist',type=Path);parser.add_argument('output',type=Path);args=parser.parse_args()
    if not args.output.resolve().is_relative_to(Path('out').resolve()):raise ValueError('Report must stay under ignored out/')
    results=[]
    for case in json.loads(args.checklist.read_text())['cases']:
        expected=appearance(Path(case['reference']))
        for fmt,path in case['exports'].items():
            row=dict(case=case['id'],format=fmt,file=path)
            try:
                actual=appearance(Path(path));row.update(passed=actual==expected,actual=actual,expected=expected)
            except Exception as error:row.update(passed=False,error=str(error))
            results.append(row)
    args.output.write_text(json.dumps(results,indent=2)+'\n')
    count=sum(r['passed'] for r in results);print(f'{count}/{len(results)} image/carrier/sampler comparisons passed')
    raise SystemExit(0 if count==len(results) else 1)


if __name__=='__main__':main()
