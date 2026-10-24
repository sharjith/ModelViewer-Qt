#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Original bounded normal/post-lit role controls; writes only under out/."""
import argparse
import ast
import copy
import hashlib
import json
import struct
from pathlib import Path
from PIL import Image
from make_texture_batch_controls import assembly
from make_texture_border_component_controls import cubes
from make_texture_control import geometry
from texture_fixture_store import TextureFixtureStore


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output_folder', type=Path)
    args = parser.parse_args()
    store = TextureFixtureStore(args.output_folder)
    positions, normals, uv, indices = cubes()
    uv = [(u*.5, v*.5) for u, v in uv]
    payload, views, accessors = bytearray(), [], []

    def accessor(values, fmt, kind, component):
        while len(payload) % 4: payload.append(0)
        offset = len(payload)
        for value in values:
            payload.extend(struct.pack('<'+fmt, *(value if isinstance(value, tuple) else (value,))))
        views.append(dict(buffer=0, byteOffset=offset, byteLength=len(payload)-offset))
        accessors.append(dict(bufferView=len(views)-1, componentType=component, count=len(values), type=kind))
        if kind == 'VEC3':
            accessors[-1].update(min=[min(v[i] for v in values) for i in range(3)],
                                 max=[max(v[i] for v in values) for i in range(3)])
        return len(accessors)-1

    p = accessor(positions, 'fff', 'VEC3', 5126)
    n = accessor(normals, 'fff', 'VEC3', 5126)
    t = accessor([(u, 1-v) for u, v in uv], 'ff', 'VEC2', 5126)
    ix = accessor(indices, 'H', 'SCALAR', 5123)
    template = dict(asset=dict(version='2.0', generator='ModelViewer original role comparison'),
        buffers=[dict(uri=store.asset(bytes(payload), '.bin'), byteLength=len(payload))],
        bufferViews=views, accessors=accessors,
        materials=[dict(doubleSided=True, pbrMetallicRoughness=dict(
            baseColorFactor=[.6,.6,.6,1], metallicFactor=0, roughnessFactor=(2/18)**.5))],
        meshes=[dict(primitives=[dict(attributes=dict(POSITION=p, NORMAL=n, TEXCOORD_0=t), indices=ix, material=0)])],
        nodes=[dict(mesh=0)], scenes=[dict(nodes=[0])], scene=0)
    normal = Image.new('RGBA', (16,16))
    color = Image.new('RGBA', (16,16))
    vectors = [(204,128,230,0), (51,128,230,64), (128,204,230,128), (128,51,230,255)]
    colors = [(255,64,32,255), (32,224,64,192), (32,64,255,128), (255,224,32,255)]
    for y in range(16):
        for x in range(16):
            cell = (x//4+y//4) % 4
            normal.putpixel((x,y), vectors[cell]); color.putpixel((x,y), colors[cell])
    neutral = Image.new('RGBA', (16,16), (128,128,255,0))
    def spec(image, role, fmt=1, mode=2, **kw):
        return dict(image=image, type=role, unit=-1 if role==4 else 0, format=fmt,
                    mode=mode, mag=2, min=2, wrap_s=2, wrap_t=2, **kw)
    cases = [
        ('normal_neutral', [spec(neutral,4)], False, False),
        ('normal_rgb', [spec(normal,4)], False, False),
        ('normal_bgr', [spec(normal,4,11)], False, False),
        ('normal_rgba_unused_alpha', [spec(normal,4,2)], False, False),
        ('normal_bgra_unused_alpha', [spec(normal,4,12)], False, False),
        ('normal_external', [spec(normal,4,external=store.image(normal))], False, False),
        ('normal_with_base', [spec(normal,4),spec(color,9,2)], False, False),
        ('post_rgb_replace', [spec(color,2,1,3)], True, False),
        ('post_rgba_replace', [spec(color,2,2,3)], True, False),
        ('post_rgb_decal_vertex_alpha', [spec(color,2,1,1)], True, True),
        ('post_external_replace', [spec(color,2,2,3,external=store.image(color))], True, False),
    ]
    authored, pairs, visual = [], [], []
    for label, specs, post, vertex in cases:
        ref = copy.deepcopy(template)
        material = ref['materials'][0]; factor = [1,1,1,1] if post else [.6,.6,.6,1]
        material['pbrMetallicRoughness']['baseColorFactor'] = factor
        if post:
            ref['extensionsUsed'] = ['KHR_materials_unlit']; material['extensions'] = dict(KHR_materials_unlit={})
        has_color = any(s['type'] in (2,9) for s in specs)
        alpha = has_color and specs[-1]['format'] in (2,12)
        if alpha or vertex: material['alphaMode'] = 'BLEND'
        ref.update(images=[],textures=[],samplers=[])
        for s in specs:
            expected = s['image'].copy()
            if s['type']==4 or s['format'] in (1,11): expected.putalpha(255)
            image_id = len(ref['images']); ref['images'].append(dict(uri=store.image(expected)))
            ref['samplers'].append(dict(wrapS=10497,wrapT=10497,magFilter=9729,minFilter=9729))
            ref['textures'].append(dict(source=image_id,sampler=image_id))
            if s['type']==4: material['normalTexture'] = dict(index=image_id)
            else: material['pbrMetallicRoughness']['baseColorTexture'] = dict(index=image_id)
        if vertex:
            # JT replaces vertex RGB but retains alpha for RGB-only Decal.
            data = struct.pack('<4f',1,1,1,.5)*len(positions)
            ref['buffers'].append(dict(uri=store.asset(data,'.bin'),byteLength=len(data)))
            ref['bufferViews'].append(dict(buffer=1,byteLength=len(data)))
            ref['accessors'].append(dict(bufferView=len(ref['bufferViews'])-1,componentType=5126,count=len(positions),type='VEC4'))
            ref['meshes'][0]['primitives'][0]['attributes']['COLOR_0'] = len(ref['accessors'])-1
        reference = 'reference_roles_'+label+'.gltf'
        (store.root/reference).write_text(json.dumps(ref,indent=2)+'\n')
        store.references[label] = reference
        mesh = store.asset(geometry(positions,uv,indices,blend=alpha or vertex,
            diffuse=(.6,.6,.6,1),vertex_color=(.25,.5,.75,.5) if vertex else None),'.jt')
        first = None
        for family,minor,version in [(10,0,1),(9,0,2),(9,5,3)]:
            for big in (False,True):
                item = store.control(f'{label}_{family}_{minor}_'+('be' if big else 'le')+'.jt',
                    assembly(big,family,minor,version,specs,list(range(20,20+len(specs))),[],mesh,''))
                authored.append(item)
                if family==10 and not big: first=item['file']
        visual += [first,reference]
        pairs.append(dict(case=label,source=first,reference=reference,role='postlit' if post else 'normal'))
    # Previous accepted lit material is a shared-shader regression control.
    for name in ['texture-homogeneous_generated_r_to_st_10_0_le.jt','reference_homogeneous_generated_r_to_st.gltf']:
        if not (store.root/name).is_file(): raise ValueError('Missing accepted regression '+name)
        visual.append(name)
    launch = store.gate.parent/'c2af-visual-files.json'
    launch.write_text(json.dumps([str(store.root/name) for name in visual],indent=2)+'\n')
    exports = [dict(id=p['case'],source=str(store.root/p['source']),reference=str(store.root/p['reference']),
        exports={fmt:str(store.gate.parent/'texture-export-ui-fixtures'/(p['case']+'.'+fmt)) for fmt in ('glb','gltf')})
        for p in pairs if p['case'] in ('normal_with_base','post_rgba_replace')]
    (store.gate.parent/'c2af-export-checklist.json').write_text(json.dumps(dict(cases=exports),indent=2)+'\n')
    store.report(dict(controls=authored,comparison_pairs=pairs,launch_file=str(launch),
        method='Original cubes and literal vector/color images; glTF reference, no importer/native execution; vendor encoding convention unverified'))
    for name in ('make_texture_role_controls.py','inspect_texture_role_exports.py'):
        path=Path(__file__).with_name(name);ast.parse(path.read_text(),filename=str(path))
    for item in authored:
        if hashlib.sha256((store.root/item['file']).read_bytes()).hexdigest()!=item['sha256']:
            raise ValueError('Authored root hash mismatch')
    for pair in pairs:
        doc=json.loads((store.root/pair['reference']).read_text());material=doc['materials'][0]
        if ('normalTexture' in material)!=(pair['role']=='normal'):raise ValueError('Wrong reference normal role')
        if ('KHR_materials_unlit' in material.get('extensions',{}))!=(pair['role']=='postlit'):raise ValueError('Wrong reference unlit role')
        for field in ('buffers','images'):
            for item in doc.get(field,[]):
                if not (store.root/item['uri']).is_file():raise ValueError('Missing reference resource')
    inventory=dict(method='Read-only authoring hash/resource/role inventory and Python AST inspection; no native or viewer execution',
        roots=len(authored),pairs=len(pairs),launch_documents=len(visual),cases=[p['case'] for p in pairs])
    (store.gate.parent/'c2af-static-inventory.json').write_text(json.dumps(inventory,indent=2)+'\n')
    print(f'Authored {len(authored)} roots; {len(pairs)} pairs / {len(visual)} launch documents')


if __name__ == '__main__': main()
