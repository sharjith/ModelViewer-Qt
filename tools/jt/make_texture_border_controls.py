#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Author original unsampled-border controls into the shared ignored asset store."""
import argparse
import json
from pathlib import Path
from PIL import Image
from make_texture_control import assembly
from make_jt9_texture_controls import assembly9
from texture_fixture_store import TextureFixtureStore


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source_folder',type=Path);parser.add_argument('transform_folder',type=Path);parser.add_argument('output_folder',type=Path)
    args=parser.parse_args();source=args.source_folder.resolve();transformed=args.transform_folder.resolve();output=args.output_folder.resolve()
    if output in (source,transformed):raise ValueError('Use a separate ignored output folder')
    store=TextureFixtureStore(output);geometry=store.file(source/'geometry.jt')
    if (source/'SOURCE_LICENSE.md').exists():store.file(source/'SOURCE_LICENSE.md')
    interior=Image.open(source/'source_image.png').convert('RGBA');image_ref=store.image(interior)
    matrix=[float(i%5==0) for i in range(16)];matrix[0]=matrix[5]=2
    authored=[]
    for label,wrap,gl_wrap in [('repeat',2,10497),('mirror',3,33648),('edge',4,33071)]:
        reference=json.loads((transformed/'tile_2x2_reference.gltf').read_text(encoding='utf-8'))
        material=reference['meshes'][0]['primitives'][0]['material']
        texture=reference['materials'][material]['pbrMetallicRoughness']['baseColorTexture']['index']
        sampler={'wrapS':gl_wrap,'wrapT':gl_wrap,'magFilter':9729,'minFilter':9729}
        reference.setdefault('samplers',[]).append(sampler);reference['textures'][texture]['sampler']=len(reference['samplers'])-1
        store.document(reference,transformed,label,{reference['textures'][texture]['source']:image_ref})
        for mode in [1,2]:
            ring=mode==2
            image=interior
            if ring:
                image=Image.new('RGBA',(interior.width+2,interior.height+2),(255,0,255,0));image.paste(interior,(1,1))
            payload=image.transpose(Image.Transpose.FLIP_TOP_BOTTOM).tobytes()
            suffix=label+('_ring' if ring else '_constant')
            for big in [False,True]:
                endian='be' if big else 'le'
                data=assembly(big,True,payload,image.width,image.height,sampler,matrix,pixel_format=2,
                              geometry_reference=geometry,border_mode=mode,image_border=int(ring))
                authored.append(store.control(f'10_0_{suffix}_{endian}.jt',data))
                for minor in [0,5]:
                    for version in [1,2,3]:
                        data=assembly9(minor,big,True,payload,image.width,image.height,matrix,[0]*4,[0.0]*16,
                            local_version=version,coordinate=0,geometry_reference=geometry,border_mode=mode,image_border=int(ring),
                            wrap_s=wrap,wrap_t=wrap,pixel_format=2,min_filter=2)
                        authored.append(store.control(f'9_{minor}_v{version}_{suffix}_{endian}.jt',data))
    store.report({'controls':authored,'method':'Original 9.x/10.0 texture roots, shared 8.2 geometry, independently authored tiled glTF UVs, opaque interior and transparent magenta border; no importer execution/vendor conformance'})
    print(f'Authored {len(authored)} positive border controls with three shared references in {store.root}')


if __name__=='__main__':main()
