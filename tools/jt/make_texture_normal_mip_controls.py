#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Original normal-vector mip/filter controls; no importer execution."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
from PIL import Image
from make_texture_batch_controls import assembly
from make_texture_border_component_controls import cubes
from make_texture_control import geometry
from make_texture_mip_controls import carrier
from texture_fixture_store import TextureFixtureStore
from inspect_texture_role_exports import appearance


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('output_folder',type=Path);args=parser.parse_args()
    store=TextureFixtureStore(args.output_folder)
    template=json.loads((store.root/'reference_roles_normal_rgb.gltf').read_text())
    combined=json.loads((store.root/'reference_roles_normal_with_base.gltf').read_text())
    positions,_,uv,indices=cubes();uv=[(u*.5,v*.5) for u,v in uv]
    base=Image.new('RGBA',(256,256))
    vectors=[(204,128,230,0),(51,128,230,64),(128,204,230,128),(128,51,230,255)]
    for y in range(256):
        for x in range(256):base.putpixel((x,y),vectors[(x//32+y//32)%4])
    tail_vectors=[(204,128,230,0),(128,204,230,64),(51,128,230,128),(128,51,230,255),
                  (128,128,255,0),(180,180,230,64),(76,76,230,128),(128,128,255,0)]
    levels=[Image.new('RGBA',(128>>i,128>>i),vector) for i,vector in enumerate(tail_vectors)]
    def opaque(image):
        out=image.copy();out.putalpha(255);return out
    cases=[('nearest_mip_nearest_rgb',1,1,3,False,True),
           ('linear_mip_nearest_bgr',11,2,4,False,True),
           ('nearest_mip_linear_rgba',2,1,5,False,True),
           ('trilinear_bgra',12,2,6,False,True),
           ('generated_trilinear',2,2,6,False,False),
           ('trilinear_with_base',2,2,6,True,True)]
    authored,pairs,visual=[],[],[]
    for label,fmt,mag,minimum,has_color,authored_mips in cases:
        reference=copy.deepcopy(combined if has_color else template)
        reference['images'][0]['uri']=carrier(store,opaque(base),[opaque(i) for i in levels] if authored_mips else [])
        reference['samplers'][0].update(magFilter=9728 if mag==1 else 9729,minFilter={3:9984,4:9985,5:9986,6:9987}[minimum])
        refname='reference_normal_mip_'+label+'.gltf'
        (store.root/refname).write_text(json.dumps(reference,indent=2)+'\n');store.references[label]=refname
        inspected=appearance(store.root/refname)
        if len(inspected['normal']['levels'])!=(9 if authored_mips else 1):raise ValueError('Reference normal levels differ')
        for field in ('buffers','images'):
            for item in reference.get(field,[]):
                if not (store.root/item['uri']).is_file():raise ValueError('Missing reference resource')
        spec=dict(image=base,mips=levels if authored_mips else [],type=4,unit=-1,format=fmt,
                  mode=2,mag=mag,min=minimum,wrap_s=2,wrap_t=2)
        specs=[spec]
        if has_color:
            color=Image.open(store.root/combined['images'][1]['uri']).convert('RGBA')
            specs.append(dict(image=color,type=9,unit=0,format=2,mode=2,mag=2,min=2,wrap_s=2,wrap_t=2))
        mesh=store.asset(geometry(positions,uv,indices,blend=has_color,diffuse=(.6,.6,.6,1)),'.jt')
        first=None
        for family,minor,version in [(10,0,1),(9,0,2),(9,5,3)]:
            for big in (False,True):
                item=store.control(f'{label}_{family}_{minor}_'+('be' if big else 'le')+'.jt',
                    assembly(big,family,minor,version,specs,list(range(20,20+len(specs))),[],mesh,''))
                authored.append(item)
                if family==10 and not big:first=item['file']
        pairs.append(dict(case=label,source=first,reference=refname,authored_levels=9 if authored_mips else 1))
        visual += [first,refname]
    visual += ['texture-normal-mip_linear_mip_nearest_bgr_9_0_be.jt',
               'texture-normal-mip_nearest_mip_linear_rgba_9_5_be.jt',
               'texture-roles_post_rgba_replace_10_0_le.jt','reference_roles_post_rgba_replace.gltf']
    if not all((store.root/f).is_file() for f in visual):raise ValueError('Missing positive control')
    for item in authored:
        if hashlib.sha256((store.root/item['file']).read_bytes()).hexdigest()!=item['sha256']:raise ValueError('Root hash mismatch')
    launch=store.gate.parent/'c2ag-visual-files.json';launch.write_text(json.dumps([str(store.root/f) for f in visual],indent=2)+'\n')
    exports=[dict(id=p['case'],source=str(store.root/p['source']),reference=str(store.root/p['reference']),
        exports={fmt:str(store.gate.parent/'texture-export-ui-fixtures'/('normal_mip_'+p['case']+'.'+fmt)) for fmt in ('glb','gltf')})
        for p in pairs if p['case'] in ('trilinear_bgra','trilinear_with_base')]
    (store.gate.parent/'c2ag-export-checklist.json').write_text(json.dumps(dict(cases=exports),indent=2)+'\n')
    store.report(dict(controls=authored,comparison_pairs=pairs,launch_file=str(launch),
        method='Original normalized vector images/cubes; independently explicit opaque reference levels using the existing private PNG carrier; no native/importer execution or authentic normal convention qualification'))
    print(f'Authored {len(authored)} roots; {len(pairs)} pairs / {len(visual)} launch documents')


if __name__=='__main__':main()
