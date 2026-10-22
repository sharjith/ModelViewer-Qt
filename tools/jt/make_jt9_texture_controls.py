#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Serialize original JT 9.0/9.5 basic texture containers; assets remain under out/.
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


def assembly9(minor, big, inline, pixels, width, height, transform, generation, planes, local_version=1):
    graph = Bytes(big)
    for ident, attrs, children, ref in [(1,[20],[2],''),(2,[],[],'geometry.jt')]:
        body = Bytes(big); body.put('HI',1,0); body.ids(attrs); body.put('H',1); body.ids(children)
        body.put('I',0); body.text(ref); body.put('7f6I',*([0]*13)); element(graph,kind('10dd103e'),ident,body)
    texture = Bytes(big); texture.put('HBIHI',1,8,0,local_version,2)
    texture.put('8I',0,2,6,2,2,2,2,0); texture.put('8f',0,0,0,1,0,0,0,1)
    texture.put('16f',*transform); texture.put('4I',*generation); texture.put('16f',*planes)
    # No coordinate-channel field in this v9 local-version-1 block.
    texture.put('IIBI',0,0,int(inline),1)
    if inline:
        texture.put('II6HBHII',1,3,2,1,width,height,1,0,1,1,len(pixels),len(pixels)); texture.data += pixels
    else:texture.text('source_image.png')
    element(graph,kind('10dd1073'),20,texture,3); end(graph); end(graph); graph.put('HI',1,0)
    guid = '12345678-1234-5678-9abc-def012345678'
    seg = Bytes(big); seg.guid(guid); seg.put('IIIIB',1,33+len(graph.data),0,len(graph.data)+1,0); seg.data += graph.data
    file = Bytes(big); file.data += f'Version 9.{minor} JT original basic texture control'.encode().ljust(75)+b' \n\r\n '
    file.put('BII',int(big),0,105+len(seg.data)); file.guid(guid); file.data += seg.data
    file.put('I',1); file.guid(guid); file.put('III',105,len(seg.data),1<<24)
    return file.data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source_folder',type=Path); parser.add_argument('transform_folder',type=Path); parser.add_argument('planar_folder',type=Path); parser.add_argument('output_folder',type=Path)
    args = parser.parse_args(); source, transformed, planar, output = [p.resolve() for p in (args.source_folder,args.transform_folder,args.planar_folder,args.output_folder)]
    if not output.is_relative_to(Path(__file__).resolve().parents[2]/'out') or output in (source,transformed,planar):
        raise ValueError('Use a separate output directory inside ignored repository out/')
    image = Image.open(source/'source_image.png').convert('RGB'); pixels = image.transpose(Image.Transpose.FLIP_TOP_BOTTOM).tobytes()
    authored = []
    for minor in (0,5):
        for case in ('stored','mirror','planar'):
            folder = output/f'9_{minor}'/case; folder.mkdir(parents=True,exist_ok=True)
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
                    data=assembly9(minor,big,inline,pixels,image.width,image.height,t,gen,planes)
                    (folder/filename).write_bytes(data);authored.append({'file':str((folder/filename).relative_to(output)),'sha256':hashlib.sha256(data).hexdigest()})
    report={'source_folder':str(source),'controls':authored,'method':'Separately serialized original v9 basic attribute/container records, 8.2 geometry companion; synthetic/reference-derived, no native or vendor execution'}
    (output/'authoring.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(f'Authored {len(authored)} original 9.0/9.5 texture controls and six glTF comparisons in {output}')


if __name__=='__main__':main()
