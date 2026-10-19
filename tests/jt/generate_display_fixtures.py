# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
# Original synthetic containers; no vendor CAD geometry is bundled.
from pathlib import Path
import argparse, struct, uuid, shutil
parser=argparse.ArgumentParser(description='Generate original JT display-selection containers referencing a locally supplied Bolt.jt')
parser.add_argument('--samples',type=Path,required=True,help='Folder containing the externally obtained Bolt.jt')
parser.add_argument('--output',type=Path,required=True,help='Output folder for the generated JT files and a local copy of Bolt.jt')
args=parser.parse_args();out=args.output;out.mkdir(parents=True,exist_ok=True);(out/'parts').mkdir(exist_ok=True)
shutil.copy2(args.samples/'Bolt.jt',out/'parts/Bolt.jt')
def fixture(name,selected=0,range_lod=False,first_missing=False,context=False,big=False):
 e='>' if big else '<'
 def n(fmt,*args):return struct.pack(e+fmt,*args)
 def guid(text):
  u=uuid.UUID(text);return n('IHH',u.time_low,u.time_mid,u.time_hi_version)+u.bytes[8:]
 def typ(first):return first+'-2ac8-11d1-9b6b-0080c7bb5997'
 def text(s):return n('i',len(s))+s.encode('utf-16-be' if big else 'utf-16-le')
 def ids(vals):return n('i',len(vals))+b''.join(n('i',i) for i in vals)
 def base(attrs):return n('BI',1,0)+ids(attrs)
 def partition(ref,children):return base([])+n('B',1)+ids(children)+n('BI',1,0)+text(ref)+n('f',0)+n('6i',*([0]*6))
 def elem(kind,id,body,base=0):return n('i',21+len(body))+guid(typ(kind))+n('Bi',base,id)+body
 end=n('i',16)+b'\xff'*16
 graph=elem('10dd103e',1,partition('',[30,40] if context else [30]))
 graph+=elem('10dd103e',2,partition('missing.jt' if first_missing else 'parts/Bolt.jt',[]))
 graph+=elem('10dd103e',3,partition('parts/Bolt.jt',[]))
 for i in range(2):
  graph+=elem('10dd102a',10+i,base([20+2*i,21+2*i])+n('Bi',1,2+i))
  graph+=elem('10dd1083',20+2*i,n('BBIIBHd',1,8,0,0,1,8,-30+60*i),3)
  body=n('BBIIBH',1,8,0,1<<6,1,0x3980)
  for color in range(4):body+=n('4f',*[(1.0 if c==3 else float(c==i) if color==1 else 0.0) for c in range(4)])
  body+=n('3f',15,0,1);graph+=elem('10dd1030',21+2*i,body,3)
 choice=base([])+n('B',1)+ids([10,11])
 if range_lod:choice+=n('BBi4f',1,1,1,10,0,0,0)
 else:choice+=n('BI',1,selected)
 graph+=elem('10dd104c' if range_lod else '10dd10f3',30,choice)
 if context:graph+=elem('10dd103e',40,partition('parts/Bolt.jt',[]))
 graph+=end
 names={100:'JT_PROP_NAME',101:name,102:'Red bolt',103:'Green bolt',104:'RangeLOD' if range_lod else 'Switch'}
 for id,value in names.items():graph+=elem('10dd106e',id,n('BIB',1,0,1)+text(value))
 graph+=end+n('Hi',1,4)
 for node,value in [(1,101),(10,102),(11,103),(30,104)]:graph+=n('4i',node,100,value,0)
 segmentId='12345678-1234-5678-9abc-def012345678'
 segment=guid(segmentId)+n('iiiiB',1,33+len(graph),0,len(graph)+1,0)+graph
 file=b'Version 10.0 JT original display selection fixture'.ljust(80,b' ')+n('Biq',int(big),0,109+len(segment))+guid(segmentId)+segment
 file+=n('i',1)+guid(segmentId)+n('qiI',109,len(segment),1<<24)
 (out/name).write_bytes(file)
fixture('switch_red.jt',0)
fixture('switch_green.jt',1)
fixture('switch_green_big_endian.jt',1,big=True)
fixture('switch_none_with_context.jt',0xffffffff,context=True)
fixture('switch_invalid.jt',2)
fixture('range_near.jt',range_lod=True)
fixture('range_fallback.jt',range_lod=True,first_missing=True)
(out/'README.txt').write_text('After rebuilding ModelViewer:\n- switch_red.jt: one red bolt at X=-30.\n- switch_green.jt and switch_green_big_endian.jt: one green bolt at X=30.\n- switch_none_with_context.jt: one grey context bolt; neither switched bolt appears.\n- switch_invalid.jt: selected child out-of-range error.\n- range_near.jt: one red bolt, default near/source-first policy.\n- range_fallback.jt: one green bolt after the red representation fails.\nAll successful fixtures have one placed mesh. Keep parts/ next to the fixtures.\nProbe only: --eye 11,0,0 on range_near.jt selects the green bolt; there is no viewer camera LOD switching yet.\n',encoding='utf-8')
print(out)
