#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Read-only normal/unlit export comparison for original C2af controls."""
import argparse
import base64
import hashlib
import io
import json
from pathlib import Path
from PIL import Image
from inspect_texture_exports import read_document, resource, LIMIT, KEY, TERMINAL


def appearance(path):
    document, binary = read_document(path)
    material = next(m for m in document['materials'] if 'normalTexture' in m or
                    'KHR_materials_unlit' in m.get('extensions', {}) or
                    'baseColorTexture' in m.get('pbrMetallicRoughness', {}))
    pbr = material.get('pbrMetallicRoughness', {})
    result = dict(unlit='KHR_materials_unlit' in material.get('extensions', {}),
        factor=pbr.get('baseColorFactor', [1,1,1,1]), metallic=pbr.get('metallicFactor',1),
        roughness=pbr.get('roughnessFactor',1), alpha=material.get('alphaMode','OPAQUE'),
        double_sided=material.get('doubleSided',False), emissive=material.get('emissiveFactor',[0,0,0]))
    for role, info in [('normal',material.get('normalTexture')),('color',pbr.get('baseColorTexture'))]:
        if info is None: continue
        texture = document['textures'][info['index']]; image = document['images'][texture['source']]
        if 'uri' in image: encoded = resource(image['uri'],path)
        else:
            view = document['bufferViews'][image['bufferView']]; buffer = document['buffers'][view.get('buffer',0)]
            payload = resource(buffer['uri'],path) if 'uri' in buffer else binary
            offset = view.get('byteOffset',0); encoded = payload[offset:offset+view['byteLength']]
        if not encoded or len(encoded)>LIMIT: raise ValueError('Invalid image size')
        pixels = Image.open(io.BytesIO(encoded))
        if pixels.width*pixels.height*4>LIMIT: raise ValueError('Decoded image exceeds limit')
        metadata=pixels.info.copy()
        levels=[pixels]+[Image.open(io.BytesIO(base64.b64decode(value,validate=True)))
                         for value in json.loads(metadata.get(KEY,'[]'))]
        signatures=[]
        for level in levels:
            if level.width*level.height*4>LIMIT:raise ValueError('Decoded mip exceeds limit')
            signatures.append(dict(size=list(level.size),rgba_sha256=hashlib.sha256(level.convert('RGBA').tobytes()).hexdigest()))
        pixels = pixels.convert('RGBA')
        sampler = document['samplers'][texture['sampler']]
        result[role] = dict(size=list(pixels.size), rgba_sha256=hashlib.sha256(pixels.tobytes()).hexdigest(),
            uv=info.get('texCoord',0), scale=info.get('scale',1),
            levels=signatures,terminal=metadata.get(TERMINAL,''),
            sampler={key:sampler.get(key) for key in ('wrapS','wrapT','magFilter','minFilter')})
    return result


def equal(a,b):
    if isinstance(a,dict) and isinstance(b,dict): return a.keys()==b.keys() and all(equal(a[k],b[k]) for k in a)
    if isinstance(a,list) and isinstance(b,list): return len(a)==len(b) and all(equal(x,y) for x,y in zip(a,b))
    if isinstance(a,(int,float)) and isinstance(b,(int,float)): return abs(a-b)<=1e-5
    return a==b


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('checklist',type=Path);parser.add_argument('output',type=Path);args=parser.parse_args()
    if not args.output.resolve().is_relative_to(Path('out').resolve()): raise ValueError('Report must stay under out/')
    rows=[]
    for case in json.loads(args.checklist.read_text())['cases']:
        expected=appearance(Path(case['reference']))
        for fmt,path in case['exports'].items():
            row=dict(case=case['id'],format=fmt,file=path)
            try:
                actual=appearance(Path(path));row.update(passed=equal(actual,expected),actual=actual,expected=expected)
            except Exception as error: row.update(passed=False,error=str(error))
            rows.append(row)
    args.output.write_text(json.dumps(rows,indent=2)+'\n')
    count=sum(r['passed'] for r in rows);print(f'{count}/{len(rows)} normal/unlit image, sampler and material comparisons passed')
    raise SystemExit(0 if count==len(rows) else 1)


if __name__=='__main__': main()
