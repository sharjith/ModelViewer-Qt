#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Original vertex-colour Replace controls; authoring only under ignored out/."""
import argparse
import json
import copy
import struct
from pathlib import Path
from PIL import Image
from make_texture_batch_controls import assembly
from make_texture_control import geometry
from make_texture_border_component_controls import cubes
from make_texture_mip_batch_controls import mapped_reference
from texture_fixture_store import TextureFixtureStore


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('affine_folder',type=Path)
    parser.add_argument('output_folder',type=Path)
    args=parser.parse_args();store=TextureFixtureStore(args.output_folder)
    prior=json.loads((args.affine_folder/'authoring.json').read_text())
    if Path(prior['shared_folder']).resolve()!=store.root:raise ValueError('Use the existing common store')
    template=json.loads((store.root/prior['references']['blend_linear']).read_text())
    positions,_,uv,indices=cubes();mapped_uv=[(u*.5,v*.5) for u,v in uv]
    vertex=(.25,.5,.75,.25)
    front={blend:store.asset(geometry(positions,mapped_uv,indices,blend=blend,
        diffuse=(.2,.7,.4,.5),vertex_color=vertex),'.jt') for blend in (False,True)}
    back=store.asset(geometry([tuple(.55*v for v in p) for p in positions],uv,indices,diffuse=(1,0,0,1)),'.jt')
    image=Image.new('RGBA',(8,8))
    for y in range(8):
        for x in range(8):image.putpixel((x,y),(255,32,64,255) if (x//2+y//2)%2 else (32,64,255,96))
    cases=[('rgb_replace',1,3,True),('rgba_replace',2,3,True),
           ('bgr_replace',11,3,True),('bgra_replace',12,3,True),
           ('rgb_replace_opaque',1,3,False),('rgba_replace_opaque',2,3,False),
           ('rgb_modulate',1,2,True),('rgba_modulate',2,2,True)]
    authored=[];visual=[];descriptive={}
    for label,fmt,mode,blend in cases:
        has_alpha=fmt in (2,12)
        ref=mapped_reference(template,store,[(u,1-v) for u,v in mapped_uv])
        pixels=image.copy() if has_alpha and blend else image.convert('RGB')
        ref['images'][0]['uri']=store.image(pixels)
        expected=[1,1,1,vertex[3] if blend and not has_alpha else 1] if mode==3 else list(vertex)
        # Constant expected vertex factors are independently expressed as a
        # glTF material factor, avoiding a second copy of the vertex stream.
        mat=ref['materials'][0];mat['pbrMetallicRoughness']['baseColorFactor']=expected
        mat['alphaMode']='BLEND' if blend else 'OPAQUE'
        for material in ref['materials']:
            material['doubleSided']=True
            material['pbrMetallicRoughness']['roughnessFactor']=(2/18)**.5
        ref['samplers'][0].update(wrapS=33071,wrapT=33071,magFilter=9729,minFilter=9729)
        hashed=store.reference(ref,label)
        named='reference_vertex_'+label+'.gltf'
        data=(store.root/hashed).read_bytes();target=store.root/named
        if target.exists() and target.read_bytes()!=data:raise ValueError('Reference alias collision')
        target.write_bytes(data);descriptive[label]=named
        if label=='rgb_replace':
            # Keep the original scalar-alpha oracle, and provide a separate
            # attribute-alpha diagnostic. A difference between them concerns
            # common renderer parity, not JT component decoding alone.
            diagnostic=copy.deepcopy(ref)
            primitive=diagnostic['meshes'][0]['primitives'][0]
            count=diagnostic['accessors'][primitive['attributes']['POSITION']]['count']
            colors=struct.pack('<4f',*expected)*count
            diagnostic['buffers'].append(dict(uri=store.asset(colors,'.bin'),byteLength=len(colors)))
            diagnostic['bufferViews'].append(dict(buffer=len(diagnostic['buffers'])-1,byteLength=len(colors)))
            diagnostic['accessors'].append(dict(bufferView=len(diagnostic['bufferViews'])-1,
                componentType=5126,count=count,type='VEC4'))
            primitive['attributes']['COLOR_0']=len(diagnostic['accessors'])-1
            diagnostic['materials'][0]['pbrMetallicRoughness']['baseColorFactor']=[1,1,1,1]
            (store.root/'diagnostic_vertex_rgb_replace_attribute_alpha.gltf').write_text(json.dumps(diagnostic,indent=2)+'\n')
        stage=dict(image=image,format=fmt,mode=mode,mag=2,min=2)
        for family,minor,version in [(10,0,1),(9,0,1),(9,5,3)]:
            for big in (False,True):
                item=store.control(f'{label}_{family}_{minor}_'+('be' if big else 'le')+'.jt',
                    assembly(big,family,minor,version,[stage],[20],[],front[blend],back))
                authored.append(item)
                if family==10 and not big:visual.append(item['file'])
        visual.append(named)
    visual+=['texture-vertex-replace_rgb_replace_9_0_be.jt',
             'texture-vertex-replace_rgba_replace_9_5_be.jt',
             'texture-affine-filter_blend_linear_10_0_le.jt']
    if not all((store.root/p).is_file() for p in visual):raise ValueError('Missing original positive control')
    (args.output_folder.parent/'c2ab-visual-files.json').write_text(json.dumps([str(store.root/p) for p in visual],indent=2)+'\n')
    store.report(dict(controls=authored,visual_files=visual,descriptive_references=descriptive,
        method='Original constant RGBA vertex-colour cubes, separate source material tint, independent expected glTF factors; shared assets; no importer execution or authentic-export claim'))
    print(f'Authored {len(authored)} positive roots and {len(visual)} visual documents')


if __name__=='__main__':main()
