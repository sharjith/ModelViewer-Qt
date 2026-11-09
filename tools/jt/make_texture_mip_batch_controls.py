#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Original C2y composed/border mip controls; authoring only, output under out/."""
import argparse
import copy
import json
import struct
from pathlib import Path
from PIL import Image
from make_texture_batch_controls import assembly, composed, BASE
from make_texture_control import geometry
from make_texture_border_component_controls import cubes
from make_texture_mip_controls import carrier
from texture_fixture_store import TextureFixtureStore


def ring(image):
    w=image.width
    out=Image.new('RGBA',(w+2,w+2),'white')
    out.paste(image,(1,1))
    for i in range(1,w+1):
        out.putpixel((0,i),(255,0,255,255));out.putpixel((w+1,i),(0,255,255,255))
        out.putpixel((i,0),(255,128,0,255));out.putpixel((i,w+1),(128,0,255,255))
    return out


def border_display(image,color=None):
    """Independent coordinate oracle: interior occupies the middle third."""
    interior=image.width if color else image.width-2
    out=Image.new('RGBA',(3*interior,3*interior),color or 'white')
    if color:out.paste(image,(interior,interior))
    else:
        for y in range(out.height):
            for x in range(out.width):
                out.putpixel((x,y),image.getpixel((min(interior+1,max(0,x-interior+1)),min(interior+1,max(0,y-interior+1)))))
    return out


def mapped_reference(template,store,uv):
    ref=copy.deepcopy(template)
    primitive=ref['meshes'][0]['primitives'][0]
    accessor=ref['accessors'][primitive['attributes']['TEXCOORD_0']]
    if accessor['componentType']!=5126 or accessor['type']!='VEC2' or accessor['count']!=len(uv):raise ValueError('Unexpected original reference layout')
    view=ref['bufferViews'][accessor['bufferView']]
    data=bytearray((store.root/ref['buffers'][view.get('buffer',0)]['uri']).read_bytes())
    offset=view.get('byteOffset',0)+accessor.get('byteOffset',0)
    for i,pair in enumerate(uv):struct.pack_into('<ff',data,offset+i*view.get('byteStride',8),*pair)
    ref['buffers'][view.get('buffer',0)]['uri']=store.asset(bytes(data),'.bin')
    return ref


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mip_folder',type=Path);parser.add_argument('output_folder',type=Path)
    args=parser.parse_args();store=TextureFixtureStore(args.output_folder)
    prior=json.loads((args.mip_folder/'authoring.json').read_text())
    if Path(prior['shared_folder']).resolve()!=store.root:raise ValueError('Use common resources')
    template=json.loads((store.root/prior['references']['authored_nearest']).read_text())
    positions,_,uv,indices=cubes()
    front=store.asset(geometry(positions,[(u*.5,v*.5) for u,v in uv],indices,blend=True,diffuse=BASE),'.jt')
    border_front=store.asset(geometry(positions,[(u-.5,v-.5) for u,v in uv],indices,blend=True,diffuse=(1,1,1,1)),'.jt')
    back=store.asset(geometry([tuple(.55*v for v in p) for p in positions],uv,indices,diffuse=(1,0,0,1)),'.jt')
    colors=[(0,64,255,255),(0,255,0,255),(255,255,0,255),(0,255,255,255),(255,0,255,255),(255,128,0,255),(255,255,255,255)]
    source=[Image.new('RGBA',(64>>i,64>>i),c) for i,c in enumerate(colors)]
    specs=[]
    for label,mode in [('decal',1),('blend',4),('add',5)]:
        levels=[im.copy() for im in source]
        if mode==1:
            for im in levels:im.putalpha(128)
        stages=[dict(image=levels[0],mips=levels[1:],mode=mode,blend=(1,.25,0,1),mag=1,min=3)]
        expected=[composed([dict(stages[0],image=im)]) for im in levels]
        specs.append((label,stages,expected,False,False))
    for label,mode in [('stack_add',5),('stack_alpha',2)]:
        second=[Image.new('RGBA',im.size,(64,0,0,255) if mode==5 else (255,255,255,128 if i%2 else 255)) for i,im in enumerate(source)]
        stages=[dict(image=source[0],mips=source[1:],mode=3,unit=0,mag=1,min=3),
                dict(image=second[0],mips=second[1:],mode=mode,unit=3,mag=1,min=3)]
        expected=[composed([dict(stages[0],image=a),dict(stages[1],image=b)]) for a,b in zip(source,second)]
        specs.append((label,stages,expected,False,False))
    for explicit in (False,True):
        for linear in (False,True):
            stored=[ring(im) if explicit else im for im in source]
            color=(255,0,255,255)
            stages=[dict(image=stored[0],mips=stored[1:],mode=2,mag=2 if linear else 1,min=6 if linear else 3,
                         border_mode=2 if explicit else 1,image_border=int(explicit),wrap_s=5,wrap_t=5,border_color=(1,0,1,1))]
            expected=[border_display(im,None if explicit else color) for im in stored]
            specs.append((('ring' if explicit else 'constant')+('_linear' if linear else '_nearest'),stages,expected,True,linear))
    second=[ring(Image.new('RGBA',im.size,(64,0,0,255))) for im in source]
    first=[ring(im) for im in source]
    stages=[dict(image=first[0],mips=first[1:],mode=3,unit=0,mag=1,min=3,border_mode=2,image_border=1,wrap_s=5,wrap_t=5),
            dict(image=second[0],mips=second[1:],mode=5,unit=3,mag=1,min=3,border_mode=2,image_border=1,wrap_s=5,wrap_t=5)]
    expected=[composed([dict(stages[0],image=border_display(a)),dict(stages[1],image=border_display(b))]) for a,b in zip(first,second)]
    specs.append(('ring_stack_add',stages,expected,True,False))
    authored=[];visual=[]
    for label,stages,expected,bordered,linear in specs:
        ref=mapped_reference(template,store,[((u-.5)/3+1/3,(1-(v-.5))/3+1/3) if bordered else (u*.5,1-v*.5) for u,v in uv])
        ref['images'][0]['uri']=carrier(store,expected[0],expected[1:],'border-3x3' if bordered else None)
        ref['materials'][0]['pbrMetallicRoughness']['baseColorFactor']=[1,1,1,1]
        ref['samplers'][0].update(wrapS=33071,wrapT=33071,magFilter=9729 if linear else 9728,minFilter=9987 if linear else 9984)
        reference=store.reference(ref,label)
        for family,minor,version in [(10,0,1),(9,0,1),(9,5,3)]:
            for big in (False,True):
                item=store.control(f'{label}_{family}_{minor}_'+('be' if big else 'le')+'.jt',assembly(big,family,minor,version,stages,list(range(20,20+len(stages))),[],border_front if bordered else front,back))
                authored.append(item)
                if family==10 and not big:visual.append(item['file'])
        visual.append(reference)
    visual+=['texture-mip-batch_stack_add_9_0_be.jt','texture-mip-batch_ring_linear_9_5_be.jt',
             'texture-mip_authored_alpha_10_0_le.jt','texture-filter_nearest_10_0_le.jt','texture-filter_linear_10_0_le.jt']
    if not all((store.root/name).is_file() for name in visual):raise ValueError('Missing positive controls')
    (args.output_folder.parent/'c2y-visual-files.json').write_text(json.dumps([str(store.root/name) for name in visual],indent=2)+'\n')
    store.report({'controls':authored,'visual_files':visual,'cases':[s[0] for s in specs],
                  'method':'Original authored levels; independent literal composition/padding oracle in private-PNG-carrier glTF comparisons. Not authentic exporter or external renderer conformance. No importer execution.'})
    print(f'Authored {len(authored)} positive roots, {len(store.references)} references, {len(visual)} visual documents')


if __name__=='__main__':main()
