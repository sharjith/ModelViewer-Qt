#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Original vivid authored-mip controls; generated data remains under out/."""
import argparse
import base64
import copy
import io
import json
from pathlib import Path
from PIL import Image, PngImagePlugin
from make_texture_batch_controls import assembly
from make_texture_control import geometry
from make_texture_border_component_controls import cubes
from texture_fixture_store import TextureFixtureStore

KEY='ModelViewer.MipChain.v1'


def carrier(store,base,levels,terminal=None):
    metadata=PngImagePlugin.PngInfo()
    if levels:
        encoded=[]
        for level in levels:
            buffer=io.BytesIO();level.save(buffer,format='PNG')
            encoded.append(base64.b64encode(buffer.getvalue()).decode())
        metadata.add_text(KEY,json.dumps(encoded,separators=(',',':')))
    if terminal:metadata.add_text("ModelViewer.MipChain.Terminal.v1",terminal)
    output=io.BytesIO();base.save(output,format='PNG',pnginfo=metadata)
    return store.asset(output.getvalue(),'.png')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('replace_folder',type=Path);parser.add_argument('output_folder',type=Path)
    args=parser.parse_args();store=TextureFixtureStore(args.output_folder)
    prior=json.loads((args.replace_folder/'authoring.json').read_text())
    if Path(prior['shared_folder']).resolve()!=store.root:raise ValueError('Use common resources')
    template=json.loads((store.root/prior['references']['rgb_modulate']).read_text())
    positions,_,uv,indices=cubes()
    front=store.asset(geometry(positions,[(u*.5,v*.5) for u,v in uv],indices,blend=True,diffuse=(1,1,1,1)),'.jt')
    back=store.asset(geometry([tuple(.55*v for v in p) for p in positions],uv,indices,diffuse=(1,0,0,1)),'.jt')
    base=Image.new('RGBA',(256,256),(0,64,255,255))
    colors=[(0,255,0,255),(255,255,0,255),(0,255,255,255),(255,0,255,255),(255,128,0,255),(255,255,255,255),(0,255,0,255),(255,255,0,255)]
    levels=[Image.new('RGBA',(128>>i,128>>i),color) for i,color in enumerate(colors)]
    alternate=[Image.new('RGBA',level.size,(255,0,128,255)) for level in levels]
    translucent=[Image.new('RGBA',level.size,tuple(colors[i][:3])+(128 if i%2==0 else 64,)) for i,level in enumerate(levels)]
    cases=[('authored_nearest',1,3,9728,9984,levels),('authored_linear',2,6,9729,9987,levels),
           ('generated_control',1,3,9728,9984,[]),('authored_variant',1,3,9728,9984,alternate),
           ('authored_alpha',1,3,9728,9984,translucent)]
    authored=[]
    for label,mag,minimum,glmag,glmin,mips in cases:
        ref=copy.deepcopy(template);ref['images'][0]['uri']=carrier(store,base,mips)
        ref['samplers'][0].update(magFilter=glmag,minFilter=glmin)
        ref['materials'][0]['pbrMetallicRoughness']['baseColorFactor']=[1,1,1,1]
        ref['materials'][0]['alphaMode']='BLEND'
        store.reference(ref,label)
        spec={'image':base,'mips':mips,'mode':2,'mag':mag,'min':minimum}
        for family,minor,version in [(10,0,1),(9,0,1),(9,5,3)]:
            for big in (False,True):
                authored.append(store.control(f'{label}_{family}_{minor}_'+('be' if big else 'le')+'.jt',
                    assembly(big,family,minor,version,[spec],[20],[],front,back)))
    previous=json.loads((args.output_folder.parent/'texture-filter-ui-fixtures'/'authoring.json').read_text())
    visual=[item['file'] for item in authored if '_10_0_le.jt' in item['file']]+list(store.references.values())
    visual+=['texture-mip_authored_nearest_9_0_le.jt','texture-mip_authored_alpha_9_5_be.jt']
    for label in ['nearest','linear']:visual+=['texture-filter_'+label+'_10_0_le.jt',previous['references'][label]]
    if not all((store.root/name).is_file() for name in visual):raise ValueError('Missing positive visual control')
    (args.output_folder.parent/'c2x-visual-files.json').write_text(json.dumps([str(store.root/name) for name in visual],indent=2)+'\n')
    store.report({'controls':authored,'visual_files':visual,'levels':[256,128,64,32,16,8,4,2,1],
        'method':'Original full mip chains with deliberate level colours and alpha; original JT envelopes; glTF comparisons use the documented ModelViewer-private PNG carrier, not a standard glTF mip extension or authentic exporter evidence'})
    print(f'Authored {len(authored)} positive JT roots, {len(store.references)} references, {len(visual)} visual documents')


if __name__=='__main__':main()
