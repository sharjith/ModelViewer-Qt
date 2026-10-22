#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Serialize original JT 9.0/9.5 basic/advanced texture containers under out/.
Uses the earlier GTEX01 comparison companions; never rewrites version headers on
another family's graph. No importer or native test is executed.
"""
import argparse
import json
from pathlib import Path
import struct
from PIL import Image
from make_texture_control import Bytes, kind, element, end, signed_pixel_component
from texture_fixture_store import TextureFixtureStore


def assembly9(minor, big, inline, pixels, width, height, transform, generation, planes, local_version=1, coordinate=0, pixel_type=3, geometry_reference='geometry.jt',image_reference='source_image.png',border_mode=0,image_border=0,wrap_s=2,wrap_t=2,pixel_format=1,min_filter=6,border_color=(0,0,0,1),back_reference=None):
    graph = Bytes(big)
    nodes=[(1,[] if back_reference else [20],[2,3] if back_reference else [2],''),(2,[20] if back_reference else [],[],geometry_reference)]
    if back_reference:nodes.append((3,[],[],back_reference))
    for ident, attrs, children, ref in nodes:
        body = Bytes(big); body.put('HI',1,0); body.ids(attrs); body.put('H',1); body.ids(children)
        body.put('I',0); body.text(ref); body.put('7f6I',*([0]*13)); element(graph,kind('10dd103e'),ident,body)
    texture = Bytes(big); texture.put('HBIH',1,8,0,local_version)
    for block in range(1,local_version+1):
        stub=block<local_version
        texture.put('I',0 if stub else 2 if block==1 else 9)
        texture.put('8I',border_mode,2,min_filter,wrap_s,wrap_t,2,2,0); texture.put('8f',0,0,0,1,*border_color)
        texture.put('16f',*transform); texture.put('4I',*generation); texture.put('16f',*planes)
        texture.put('IIBI',0,0,int(inline),0 if stub else 1)
        if not stub:
            if inline:
                texture.put('II6HBHII',pixel_format,pixel_type,2,1,width,height,1,image_border,1,1,len(pixels),len(pixels)); texture.data += pixels
            else:texture.text(image_reference)
        # V3 appends this field; modern v10 places it before image storage.
        if block==3:texture.put('i',coordinate)
    element(graph,kind('10dd1073'),20,texture,3); end(graph); end(graph); graph.put('HI',1,0)
    guid = '12345678-1234-5678-9abc-def012345678'
    seg = Bytes(big); seg.guid(guid); seg.put('IIIIB',1,33+len(graph.data),0,len(graph.data)+1,0); seg.data += graph.data
    file = Bytes(big); file.data += f'Version 9.{minor} JT original texture v{local_version} control'.encode().ljust(75)+b' \n\r\n '
    file.put('BII',int(big),0,105+len(seg.data)); file.guid(guid); file.data += seg.data
    file.put('I',1); file.guid(guid); file.put('III',105,len(seg.data),1<<24)
    return file.data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source_folder',type=Path); parser.add_argument('transform_folder',type=Path); parser.add_argument('planar_folder',type=Path); parser.add_argument('output_folder',type=Path)
    parser.add_argument('--advanced',action='store_true',help='Author local versions 2/3 in separate folders instead of basic version 1')
    numeric=parser.add_mutually_exclusive_group()
    numeric.add_argument('--unsigned16',action='store_true',help='Use unsigned 16-bit inline RGB components')
    numeric.add_argument('--unsigned32',action='store_true',help='Use unsigned 32-bit inline RGB components')
    numeric.add_argument('--float32',action='store_true',help='Use normalized float32 inline RGB components')
    numeric.add_argument('--float16',action='store_true',help='Use normalized IEEE binary16 inline RGB components')
    numeric.add_argument('--signed-bits',type=int,choices=[8,16,32],help='Use GL2-profile signed integer components')
    args = parser.parse_args(); source, transformed, planar, output = [p.resolve() for p in (args.source_folder,args.transform_folder,args.planar_folder,args.output_folder)]
    pixel_type={8:1,16:6,32:8}.get(args.signed_bits) or (9 if args.float16 else 2 if args.float32 else 7 if args.unsigned32 else 5 if args.unsigned16 else 3)
    if not output.is_relative_to(Path(__file__).resolve().parents[2]/'out') or output in (source,transformed,planar):
        raise ValueError('Use a separate output directory inside ignored repository out/')
    image = Image.open(source/'source_image.png').convert('RGB'); pixels = image.transpose(Image.Transpose.FLIP_TOP_BOTTOM).tobytes()
    store=TextureFixtureStore(output);authored=[];obsolete=[]
    for minor in (0,5):
        for version in ((2,3) if args.advanced else (1,)):
            for case in ('stored','mirror','planar'):
                folder = output/f'9_{minor}'
                if args.advanced:folder=folder/f'v{version}'
                folder=folder/case; folder.mkdir(parents=True,exist_ok=True)
                base = planar if case=='planar' else transformed if case=='mirror' else source
                for path in base.iterdir():
                    if path.suffix in ('.png','.bin') or path.name in ('geometry.jt','SOURCE_LICENSE.md'):
                        store.file(path);obsolete.append(folder/path.name)
                ref = base/('planar_xy_reference.gltf' if case=='planar' else 'mirror_u_reference.gltf' if case=='mirror' else 'reference.gltf')
                case_label=f'9_{minor}/'+(f'v{version}/' if args.advanced else '')+case
                geometry_reference=store.file(base/'geometry.jt');image_reference=store.file(base/'source_image.png')
                reference=json.loads(ref.read_text(encoding='utf-8'));overrides=None
                if args.signed_bits==8:
                    expected=image.point(lambda value:signed_pixel_component(value,8,False)[1])
                    image_reference=store.image(expected)
                    material=reference['meshes'][0]['primitives'][0]['material']
                    texture=reference['materials'][material]['pbrMetallicRoughness']['baseColorTexture']['index']
                    overrides={reference['textures'][texture]['source']:image_reference}
                store.document(reference,base,case_label,overrides)
                obsolete.append(folder/'reference.gltf')
                t = [float(i%5==0) for i in range(16)]; gen = [0]*4; planes = [0.0]*16
                if case=='mirror':t[0],t[12] = -1,1
                if case=='planar':gen[:2]=[1,1];planes[0]=planes[5]=1
                for big in (False,True):
                    for inline in (False,True):
                        filename=f'texture_{"inline" if inline else "external"}_{"be" if big else "le"}.jt'
                        coordinate=1 if version==3 and case=='planar' else 0
                        packed=pixels
                        if args.unsigned16:
                            packed=b''.join((0 if value==0 else 65535 if value==255 else value*257+73).to_bytes(2,'big' if big else 'little') for value in pixels)
                        if args.unsigned32:
                            packed=b''.join((0 if value==0 else 0xffffffff if value==255 else value*0x01010101+0x1234).to_bytes(4,'big' if big else 'little') for value in pixels)
                        if args.float32 or args.float16:
                            packed=b''.join(struct.pack(('>' if big else '<')+('e' if args.float16 else 'f'),value/255.0) for value in pixels)
                        if args.signed_bits:
                            packed=b''.join(signed_pixel_component(value,args.signed_bits,big)[0] for value in pixels)
                        data=assembly9(minor,big,inline,packed,image.width,image.height,t,gen,planes,version,coordinate,pixel_type,geometry_reference,image_reference)
                        authored.append(store.control(case_label+'/'+filename,data));obsolete.append(folder/filename)
    report={'source_folder':str(source),'controls':authored,'method':'Separately serialized original v9 attribute/container records with compatibility stubs for advanced versions; 8.2 geometry companion; synthetic/reference-derived, no native or vendor execution'}
    store.report(report);store.cleanup(obsolete)
    print(f'Authored {len(authored)} controls in {store.root}; gate manifest: {output}/authoring.json')


if __name__=='__main__':main()
