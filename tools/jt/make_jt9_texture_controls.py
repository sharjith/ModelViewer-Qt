#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Serialize original JT 9.0/9.5 basic/advanced texture containers under out/.
Uses the earlier GTEX01 comparison companions; never rewrites version headers on
another family's graph. No importer or native test is executed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
from PIL import Image
from make_texture_control import Bytes, kind, element, end


def assembly9(minor, big, inline, pixels, width, height, transform, generation, planes, local_version=1, coordinate=0):
    graph = Bytes(big)
    for ident, attrs, children, ref in [(1,[20],[2],''),(2,[],[],'geometry.jt')]:
        body = Bytes(big); body.put('HI',1,0); body.ids(attrs); body.put('H',1); body.ids(children)
        body.put('I',0); body.text(ref); body.put('7f6I',*([0]*13)); element(graph,kind('10dd103e'),ident,body)
    texture = Bytes(big); texture.put('HBIH',1,8,0,local_version)
    for block in range(1,local_version+1):
        stub=block<local_version
        texture.put('I',0 if stub else 2 if block==1 else 9)
        texture.put('8I',0,2,6,2,2,2,2,0); texture.put('8f',0,0,0,1,0,0,0,1)
        texture.put('16f',*transform); texture.put('4I',*generation); texture.put('16f',*planes)
        texture.put('IIBI',0,0,int(inline),0 if stub else 1)
        if not stub:
            if inline:
                texture.put('II6HBHII',1,3,2,1,width,height,1,0,1,1,len(pixels),len(pixels)); texture.data += pixels
            else:texture.text('source_image.png')
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
    args = parser.parse_args(); source, transformed, planar, output = [p.resolve() for p in (args.source_folder,args.transform_folder,args.planar_folder,args.output_folder)]
    if not output.is_relative_to(Path(__file__).resolve().parents[2]/'out') or output in (source,transformed,planar):
        raise ValueError('Use a separate output directory inside ignored repository out/')
    image = Image.open(source/'source_image.png').convert('RGB'); pixels = image.transpose(Image.Transpose.FLIP_TOP_BOTTOM).tobytes()
    authored = []
    for minor in (0,5):
        for version in ((2,3) if args.advanced else (1,)):
            for case in ('stored','mirror','planar'):
                folder = output/f'9_{minor}'
                if args.advanced:folder=folder/f'v{version}'
                folder=folder/case; folder.mkdir(parents=True,exist_ok=True)
                base = planar if case=='planar' else transformed if case=='mirror' else source
                for path in base.iterdir():
                    if path.suffix in ('.png','.bin') or path.name in ('geometry.jt','SOURCE_LICENSE.md'):
                        shutil.copyfile(path,folder/path.name)
                ref = base/('planar_xy_reference.gltf' if case=='planar' else 'mirror_u_reference.gltf' if case=='mirror' else 'reference.gltf')
                shutil.copyfile(ref,folder/'reference.gltf')
                t = [float(i%5==0) for i in range(16)]; gen = [0]*4; planes = [0.0]*16
                if case=='mirror':t[0],t[12] = -1,1
                if case=='planar':gen[:2]=[1,1];planes[0]=planes[5]=1
                for big in (False,True):
                    for inline in (False,True):
                        filename=f'texture_{"inline" if inline else "external"}_{"be" if big else "le"}.jt'
                        coordinate=1 if version==3 and case=='planar' else 0
                        data=assembly9(minor,big,inline,pixels,image.width,image.height,t,gen,planes,version,coordinate)
                        (folder/filename).write_bytes(data);authored.append({'file':str((folder/filename).relative_to(output)),'sha256':hashlib.sha256(data).hexdigest()})
    report={'source_folder':str(source),'controls':authored,'method':'Separately serialized original v9 attribute/container records with compatibility stubs for advanced versions; 8.2 geometry companion; synthetic/reference-derived, no native or vendor execution'}
    (output/'authoring.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(f'Authored {len(authored)} original 9.0/9.5 texture controls and reference comparisons in {output}')


if __name__=='__main__':main()
