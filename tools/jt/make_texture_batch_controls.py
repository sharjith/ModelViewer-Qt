#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Original combined combiner/unit/inheritance controls; no importer execution."""
import argparse
import copy
import json
import math
from pathlib import Path
from PIL import Image
from make_texture_control import Bytes,element,end,kind,geometry
from make_texture_border_component_controls import cubes
from make_texture_border_format_controls import convert
from texture_fixture_store import TextureFixtureStore

IDENTITY=[float(i%5==0) for i in range(16)]
BASE=(.25,.5,.75,.5)


def attribute(big,family,version,spec):
    b=Bytes(big);flags=spec.get('flags',8);inhibit=spec.get('inhibit',0);final=spec.get('final',0)
    b.put('HBIH' if family==9 else 'BBIIB',1,flags,inhibit,*(() if family==9 else (final,)),version)
    for block in range(1,(version if family==9 else 1)+1):
        stub=family==9 and block<version;role=spec.get('type',9)
        if family==9 and block==1 and role==9:role=2
        b.put('I',0 if stub else role)
        b.put('8I',0,spec.get('mag',1),spec.get('min',1),4,4,2,spec.get('mode',2),0)
        b.put('8f',*spec.get('blend',(0,0,0,1)),0,0,0,1)
        b.put('16f',*IDENTITY);b.put('4I',0,0,0,0);b.put('16f',*([0]*16))
        b.put('i',0 if stub else spec.get('unit',0))
        if family==10:b.put('i',0)
        present=not stub and spec.get('image') is not None
        external=spec.get('external');b.put('IBI',0,int(not external),int(present))
        if present:
            if external:b.text(external)
            else:
                image=spec['image'];fmt=spec.get('format',2);rows=[]
                for y in range(image.height):
                    row=bytearray()
                    for x in range(image.width):row.extend(convert(image.getpixel((x,y)),fmt)[0])
                    rows.append(bytes(row))
                pixels=b''.join(reversed(rows))
                b.put('II6H',fmt,3,2,1,image.width,image.height,1,0)
                b.put('B' if family==9 else 'I',0);b.put('HII',1,len(pixels),len(pixels));b.data+=pixels
        if family==9 and block==3:b.put('i',0)
    return b


def assembly(big,family,minor,version,specs,parent,child,front,back):
    graph=Bytes(big)
    for ident,ref,children,attrs in [(1,'',[2,3],[]),(2,'',[4],parent),(3,back,[],[]),(4,front,[],child)]:
        b=Bytes(big);b.put('HI' if family==9 else 'BI',1,0);b.ids(attrs)
        b.put('H' if family==9 else 'B',1);b.ids(children)
        if family==10:b.put('B',1)
        b.put('I',0);b.text(ref);b.put('7f6I' if family==9 else 'f6I',*([0]*(13 if family==9 else 7)))
        element(graph,kind('10dd103e'),ident,b)
    for i,spec in enumerate(specs):element(graph,kind('10dd1073'),20+i,attribute(big,family,version,spec),3)
    end(graph);end(graph);graph.put('HI',1,0)
    guid='12345678-1234-5678-9abc-def012345678';segment=Bytes(big);segment.guid(guid);segment.put('IIIIB',1,33+len(graph.data),0,1+len(graph.data),0);segment.data+=graph.data
    file=Bytes(big);file.data+=f'Version {family}.{minor} JT original appearance batch'.encode().ljust(75)+b' \n\r\n '
    file.put('BI',int(big),0);file.put('I' if family==9 else 'Q',(105 if family==9 else 109)+len(segment.data));file.guid(guid);file.data+=segment.data
    file.put('I',1);file.guid(guid);file.put('I' if family==9 else 'Q',105 if family==9 else 109);file.put('II',len(segment.data),1<<24)
    return file.data


def composed(stages):
    """Independent normalized linear-colour oracle, encoded as sRGB PNG bytes."""
    def linear(v):return v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4
    def encoded(v):return 12.92*v if v<=.0031308 else 1.055*v**(1/2.4)-.055
    output=Image.new('RGBA',(2,2))
    for y in range(2):
        for x in range(2):
            rgb=list(BASE[:3]);alpha=BASE[3]
            for stage in stages:
                fmt=stage.get('format',2);_,pixel=convert(stage['image'].getpixel((x,y)),fmt)
                cs=[linear(v/255) for v in pixel[:3]];a=pixel[3]/255;mode=stage.get('mode',2)
                has_rgb=fmt!=10;has_alpha=fmt in (2,4,10,12)
                if has_rgb:
                    if mode==1:rgb=[rgb[i]*(1-a)+cs[i]*a for i in range(3)] if has_alpha else cs
                    elif mode==2:rgb=[rgb[i]*cs[i] for i in range(3)]
                    elif mode==3:rgb=cs
                    elif mode==4:rgb=[rgb[i]*(1-cs[i])+stage.get('blend',(0,0,0,1))[i]*cs[i] for i in range(3)]
                    elif mode==5:rgb=[min(1,rgb[i]+cs[i]) for i in range(3)]
                if has_alpha:
                    if mode==3:alpha=a
                    elif mode!=1:alpha*=a
            output.putpixel((x,y),tuple(math.floor(max(0,min(1,encoded(v)))*255+.5) for v in rgb)+(math.floor(alpha*255+.5),))
    return output


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('replace_folder',type=Path);parser.add_argument('output_folder',type=Path);args=parser.parse_args()
    store=TextureFixtureStore(args.output_folder);prior=json.loads((args.replace_folder/'authoring.json').read_text())
    if Path(prior['shared_folder']).resolve()!=store.root:raise ValueError('Use the existing shared store')
    template=json.loads((store.root/prior['references']['rgb_modulate']).read_text())
    positions,_,source_uv,indices=cubes();uv=[(u*.5,v*.5) for u,v in source_uv]
    front=store.asset(geometry(positions,uv,indices,blend=True,diffuse=BASE),'.jt')
    back=store.asset(geometry([tuple(.55*v for v in p) for p in positions],source_uv,indices,diffuse=(1,0,0,1)),'.jt')
    template['samplers'][0].update(magFilter=9728,minFilter=9728)
    a=Image.new('RGBA',(2,2));a.putdata([(255,64,32,255),(32,255,64,192),(64,32,255,64),(255,255,255,128)])
    b=Image.new('RGBA',(2,2));b.putdata([(32,192,255,128),(255,128,32,64),(128,255,32,192),(32,64,255,255)])
    c=a.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
    def texture(mode=2,unit=0,image=a,**kw):return dict(image=image,mode=mode,unit=unit,**kw)
    blend=(1,.25,0,1);reset=dict(type=26,image=None);automatic=texture(unit=-1,type=4,format=1)
    # Each recipe separately states the expected effective stages; it does not
    # ask the JT writer or importer to resolve its own inheritance for the oracle.
    cases=[
        ('decal_rgba',[texture(1)],[20],[],[texture(1)],False),
        ('blend_rgba',[texture(4,blend=blend)],[20],[],[texture(4,blend=blend)],False),
        ('add_rgba',[texture(5)],[20],[],[texture(5)],False),
        ('replace_luma',[texture(3,format=4)],[20],[],[texture(3,format=4)],False),
        ('replace_alpha',[texture(3,format=10)],[20],[],[texture(3,format=10)],False),
        ('replace_external',[texture(3,external=store.image(a))],[20],[],[texture(3)],False),
        ('unit_order',[texture(),texture(3,3,b)],[21,20],[],[texture(),texture(3,3,b)],False),
        ('unit_accumulate',[texture(),texture(5,3,b)],[20,21],[],[texture(),texture(5,3,b)],False),
        ('inherit_override',[texture(),texture(5,3,b),texture(3,0,c)],[20,21],[22],[texture(3,0,c),texture(5,3,b)],False),
        ('inhibit_combiner',[texture(4,blend=blend),texture(3,0,b,inhibit=16)],[20],[21],[texture(4,0,b,blend=blend)],False),
        ('final_combiner',[texture(4,blend=blend,final=16),texture(3,0,b)],[20],[21],[texture(4,0,b,blend=blend)],True),
        ('forced_override',[texture(4,blend=blend,flags=9),texture(3,0,b,flags=10)],[20],[21],[texture(3,0,b)],False),
        ('reset_units',[texture(),texture(5,3,b),reset],[20,21],[22],[],True),
        ('final_reset_fence',[dict(reset,flags=9),texture()],[20],[21],[],True),
        ('automatic_conflict',[automatic,texture(),texture(5,3,b)],[20,21,22,20],[],[texture(),texture(5,3,b)],True),
        ('none_upper',[texture(),texture(0,3,b)],[20,21],[],[texture()],False)]
    authored=[];case_metadata=[]
    for label,specs,parent,child,effective,modern_only in cases:
        reference=copy.deepcopy(template);material=reference['materials'][0];material['alphaMode']='BLEND'
        if not effective:
            material['pbrMetallicRoughness'].pop('baseColorTexture',None);material['pbrMetallicRoughness']['baseColorFactor']=list(BASE)
        elif len(effective)==1 and effective[0]['mode']==2:
            reference['images'][0]['uri']=store.image(effective[0]['image']);material['pbrMetallicRoughness']['baseColorFactor']=list(BASE)
        else:
            reference['images'][0]['uri']=store.image(composed(effective));material['pbrMetallicRoughness']['baseColorFactor']=[1,1,1,1]
        store.reference(reference,label)
        for big in (False,True):
            endian='be' if big else 'le'
            authored.append(store.control(f'{label}_10_0_{endian}.jt',assembly(big,10,0,1,specs,parent,child,front,back)))
            for minor,version in ((0,1),(5,2),(5,3)):
                if label=='final_combiner':continue
                if modern_only and version!=3:continue
                authored.append(store.control(f'{label}_9_{minor}_v{version}_{endian}.jt',assembly(big,9,minor,version,specs,parent,child,front,back)))
        case_metadata.append({'case':label,'source_roles':len(specs),'expected_colour_stages':len(effective),'modern_only_field_final':label=='final_combiner'})
    store.report({'controls':authored,'cases':case_metadata,'method':'Original combiner/unit graphs and independently resolved expected recipes; linear-colour composition and sRGB canonical references; common original cubes/buffers; synthetic, no importer execution or exporter conformance'})
    print(f'Authored {len(authored)} positive batch roots and {len(set(store.references.values()))} unique shared references for {len(cases)} cases')


if __name__=='__main__':main()
