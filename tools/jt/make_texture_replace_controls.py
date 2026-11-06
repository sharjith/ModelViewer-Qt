#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Original Replace versus Modulate controls with tinted nested cubes."""
import argparse
import copy
import json
import struct
from pathlib import Path
from PIL import Image
from make_texture_control import assembly,geometry
from make_jt9_texture_controls import assembly9
from make_texture_border_component_controls import cubes
from texture_fixture_store import TextureFixtureStore


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('component_folder',type=Path);parser.add_argument('output_folder',type=Path);args=parser.parse_args()
    store=TextureFixtureStore(args.output_folder);prior=json.loads((args.component_folder/'authoring.json').read_text())
    if Path(prior['shared_folder']).resolve()!=store.root:raise ValueError('Use the existing common control store')
    template=json.loads((store.root/prior['references']['luminance_opaque']).read_text())
    positions,_,source_uv,indices=cubes();uv=[(u*.5,v*.5) for u,v in source_uv];tint=(.2,.7,.4,.5)
    front=store.asset(geometry(positions,uv,indices,blend=True,diffuse=tint),'.jt')
    back=store.asset(geometry([tuple(.55*v for v in p) for p in positions],source_uv,indices,diffuse=(1,0,0,1)),'.jt')
    mapped=b''.join(struct.pack('<ff',u,1-v) for u,v in uv)
    template['buffers'].append({'uri':store.asset(mapped,'.bin'),'byteLength':len(mapped)})
    template['bufferViews'].append({'buffer':len(template['buffers'])-1,'byteLength':len(mapped)})
    template['accessors'].append({'bufferView':len(template['bufferViews'])-1,'componentType':5126,'count':len(uv),'type':'VEC2'})
    template['meshes'][0]['primitives'][0]['attributes']['TEXCOORD_0']=len(template['accessors'])-1
    template['nodes'][0]['name']='Textured outer cube'
    identity=[float(i%5==0) for i in range(16)];sampler={'wrapS':33071,'wrapT':33071,'magFilter':9729,'minFilter':9729};authored=[]
    for label,fmt in [('rgb',1),('rgba',2)]:
        image=Image.new('RGBA',(2,2));image.putdata([(255,64,32,255),(32,255,64,255),(64,32,255,64),(255,255,255,255)])
        if fmt==1:image=image.convert('RGB')
        payload=image.transpose(Image.Transpose.FLIP_TOP_BOTTOM).tobytes()
        for mode,name in [(2,'modulate'),(3,'replace')]:
            case=label+'_'+name;reference=copy.deepcopy(template);reference['images'][0]['uri']=store.image(image)
            reference['materials'][0]['alphaMode']='BLEND'
            reference['materials'][0]['pbrMetallicRoughness']['baseColorFactor']=list(tint) if mode==2 else [1,1,1,1 if fmt==2 else .5]
            store.reference(reference,case)
            for big in (False,True):
                endian='be' if big else 'le'
                authored.append(store.control(f'{case}_10_0_{endian}.jt',assembly(big,True,payload,2,2,sampler,pixel_format=fmt,geometry_reference=front,back_reference=back,blend_type=mode)))
                for minor,version in ((0,1),(5,2),(5,3)):
                    authored.append(store.control(f'{case}_9_{minor}_v{version}_{endian}.jt',assembly9(minor,big,True,payload,2,2,identity,[0]*4,[0.0]*16,local_version=version,coordinate=0,geometry_reference=front,back_reference=back,pixel_format=fmt,wrap_s=4,wrap_t=4,min_filter=2,blend_type=mode)))
    store.report({'controls':authored,'method':'Original tinted nested cubes and independently authored glTF material factors for RGB/RGBA Replace versus Modulate; existing common geometry buffers reused; synthetic only, no importer execution or exporter conformance'})
    print(f'Authored {len(authored)} positive combiner roots and four shared references in {store.root}')


if __name__=='__main__':main()
