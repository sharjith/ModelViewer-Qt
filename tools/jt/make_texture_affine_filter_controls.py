#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Original C2z affine filtering/colour-ring controls; authoring only under out/."""
import argparse
import json
from pathlib import Path
from PIL import Image
from make_texture_batch_controls import assembly, composed, BASE
from make_texture_mip_batch_controls import ring, border_display, mapped_reference
from make_texture_border_format_controls import convert
from make_texture_border_component_controls import cubes
from make_texture_control import geometry
from make_texture_mip_controls import carrier
from texture_fixture_store import TextureFixtureStore


def canonical(image, fmt):
    result=Image.new('RGBA',image.size)
    result.putdata([convert(image.getpixel((x,y)),fmt)[1] for y in range(image.height) for x in range(image.width)])
    return result


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
    colors=[(0,64,255,255),(0,255,0,128),(255,255,0,255),(0,255,255,128),(255,0,255,255),(255,128,0,128),(255,255,255,255)]
    levels=[]
    for level,color in enumerate(colors):
        width=64>>level;image=Image.new('RGBA',(width,width),color)
        if width>=4:
            for y in range(width):
                for x in range(width):
                    if (x//max(1,width//8)+y//max(1,width//8))%2:image.putpixel((x,y),(255,32,64,128))
        levels.append(image)
    specs=[]
    for label,mode,fmt,mipped in [('blend_linear',4,2,False),('blend_trilinear',4,2,True),('decal_rgb_trilinear',1,1,True)]:
        source=levels if mipped else levels[:1]
        stage=dict(image=source[0],mips=source[1:],mode=mode,format=fmt,blend=(1,.25,0,1),mag=2,min=6 if mipped else 2)
        expected=[composed([dict(stage,image=image)]) for image in source]
        specs.append((label,stage,expected,False))
    for fmt,label in [(1,'rgb'),(2,'rgba'),(3,'luminance'),(4,'luminance_alpha'),(7,'red'),(8,'green'),(9,'blue'),(10,'alpha'),(11,'bgr'),(12,'bgra')]:
        source=[ring(image) for image in levels]
        stage=dict(image=source[0],mips=source[1:],mode=2,format=fmt,mag=2,min=6,border_mode=2,image_border=1,wrap_s=5,wrap_t=5)
        expected=[border_display(canonical(image,fmt)) for image in source]
        specs.append(('ring_'+label,stage,expected,True))
    authored=[];visual=[]
    for label,stage,expected,bordered in specs:
        ref=mapped_reference(template,store,[((u-.5)/3+1/3,(1-(v-.5))/3+1/3) if bordered else (u*.5,1-v*.5) for u,v in uv])
        ref['images'][0]['uri']=carrier(store,expected[0],expected[1:],'border-3x3' if bordered else None) if len(expected)>1 else store.image(expected[0])
        ref['materials'][0]['pbrMetallicRoughness']['baseColorFactor']=[1,1,1,1]
        # The original JT geometry has default two-sided materials and Phong
        # exponent 16. Match those raster properties before comparing overlap.
        for material in ref['materials']:
            material['doubleSided']=True
            material['pbrMetallicRoughness']['roughnessFactor']=(2/18)**.5
        ref['samplers'][0].update(wrapS=33071,wrapT=33071,magFilter=9729,minFilter=9987 if len(expected)>1 else 9729)
        reference=store.reference(ref,label)
        for family,minor,version in [(10,0,1),(9,0,1),(9,5,3)]:
            for big in (False,True):
                item=store.control(f'{label}_{family}_{minor}_'+('be' if big else 'le')+'.jt',assembly(big,family,minor,version,[stage],[20],[],border_front if bordered else front,back))
                authored.append(item)
                if family==10 and not big:visual.append(item['file'])
        visual.append(reference)
    visual+=['texture-affine-filter_blend_trilinear_9_0_be.jt','texture-affine-filter_ring_luminance_alpha_9_5_be.jt',
             'texture-mip-batch_ring_linear_10_0_le.jt','texture-filter_nearest_10_0_le.jt','texture-filter_linear_10_0_le.jt']
    # RGB/BGR and RGBA/BGRA deliberately share identical expected resources.
    # Open each reference only once in the owner's single-instance checklist.
    visual=list(dict.fromkeys(visual))
    if not all((store.root/name).is_file() for name in visual):raise ValueError('Missing positive controls')
    (args.output_folder.parent/'c2z-visual-files.json').write_text(json.dumps([str(store.root/name) for name in visual],indent=2)+'\n')
    store.report({'controls':authored,'visual_files':visual,'cases':[s[0] for s in specs],
                  'method':'Original colour/checker mip levels and component packing; independent affine composition/ring oracle; private-carrier glTF references share renderer. No authentic exporter or importer execution.'})
    print(f'Authored {len(authored)} positive roots, {len(store.references)} reference mappings ({len(set(store.references.values()))} unique), {len(visual)} visual documents')


if __name__=='__main__':main()
