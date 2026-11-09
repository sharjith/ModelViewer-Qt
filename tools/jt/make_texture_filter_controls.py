#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Original paired sampler controls, sharing existing nested-cube resources."""
import argparse
import copy
import json
from pathlib import Path
from PIL import Image
from make_texture_batch_controls import assembly, BASE
from make_texture_control import geometry
from make_texture_border_component_controls import cubes
from texture_fixture_store import TextureFixtureStore


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('replace_folder',type=Path)
    parser.add_argument('output_folder',type=Path)
    args=parser.parse_args()
    store=TextureFixtureStore(args.output_folder)
    prior=json.loads((args.replace_folder/'authoring.json').read_text())
    if Path(prior['shared_folder']).resolve()!=store.root:raise ValueError('Use the common resource folder')
    template=json.loads((store.root/prior['references']['rgb_modulate']).read_text())
    positions,_,uv,indices=cubes()
    front=store.asset(geometry(positions,[(u*.5,v*.5) for u,v in uv],indices,blend=True,diffuse=BASE),'.jt')
    back=store.asset(geometry([tuple(.55*v for v in p) for p in positions],uv,indices,diffuse=(1,0,0,1)),'.jt')
    image=Image.new('RGBA',(4,4))
    for y in range(4):
        for x in range(4):image.putpixel((x,y),(255,32,0,255) if (x+y)%2==0 else (0,64,255,96))
    image_uri=store.image(image)
    authored=[]
    for label,mag,minimum,glmag,glmin in [
        ('nearest',1,1,9728,9728),('linear',2,2,9729,9729),
        ('nearest_mip_nearest',1,3,9728,9984),('linear_mip_nearest',2,4,9729,9985),
        ('nearest_mip_linear',1,5,9728,9986),('linear_mip_linear',2,6,9729,9987)]:
        ref=copy.deepcopy(template)
        ref['images'][0]['uri']=image_uri
        ref['samplers'][0].update(magFilter=glmag,minFilter=glmin)
        ref['materials'][0]['pbrMetallicRoughness']['baseColorFactor']=list(BASE)
        ref['materials'][0]['alphaMode']='BLEND'
        store.reference(ref,label)
        spec={'image':image,'mode':2,'mag':mag,'min':minimum}
        for family,minor,version in [(10,0,1),(9,0,1),(9,5,3)]:
            for big in (False,True):
                suffix=f'{family}_{minor}_'+('be' if big else 'le')+'.jt'
                authored.append(store.control(label+'_'+suffix,assembly(big,family,minor,version,[spec],[20],[],front,back)))
    batch=json.loads((args.output_folder.parent/'texture-batch-ui-fixtures'/'authoring.json').read_text())
    visual=[item['file'] for item in authored if '_10_0_le.jt' in item['file']]
    visual+=list(store.references.values())
    visual+=['texture-filter_nearest_9_0_le.jt','texture-filter_linear_9_5_be.jt']
    for case in ['unit_accumulate','blend_rgba']:
        visual+=['texture-batch_'+case+'_10_0_le.jt',batch['references'][case]]
    if not all((store.root/name).is_file() for name in visual):raise ValueError('Missing visual resource')
    (args.output_folder.parent/'c2w-visual-files.json').write_text(json.dumps([str(store.root/name) for name in visual],indent=2)+'\n')
    store.report({'controls':authored,'visual_files':visual,'method':'Original sampler-only variants with shared original cubes/checker and paired glTF sampler declarations; generated mipmaps, not authored JT mip payload qualification'})
    print(f'Authored {len(authored)} positive filter roots with six shared references')


if __name__=='__main__':main()
