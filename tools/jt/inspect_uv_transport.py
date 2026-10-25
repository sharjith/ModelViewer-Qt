#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
# Original forensic adapter, 2026-10-25. Models the existing GPL JT packet reader
# and the retained Siemens JT10 reference's integer/hash algorithms. Jenkins'
# lookup2 word hash is described in reference Annex C (Bob Jenkins, 1996).
# This reads containers and attributes only: no viewer/native build/test, topology
# reconstruction, geometry export or mutation of input files. Only modern LE
# triangle payloads in the inspected profile are qualified. Other profiles report
# audit errors. Reports contain sample-derived values and local paths: keep local.
from pathlib import Path
import struct, lzma, zlib, json, hashlib, uuid, collections, math
import argparse
parser=argparse.ArgumentParser(description="Read-only partial modern JT UV transport audit; not full geometry validation.")
parser.add_argument("input",type=Path,help="One JT or a directory containing an assembly and companion JTs")
parser.add_argument("--output",type=Path,required=True)
args=parser.parse_args()
ROOT=args.input.resolve();OUT=args.output.resolve()
REPO=Path(__file__).resolve().parents[2]
if OUT.is_relative_to(REPO) and not OUT.is_relative_to(REPO/'out'):
 parser.error("Repository-local output must stay under out/")
if ROOT==OUT:parser.error("Output must not overwrite input")
OUT.parent.mkdir(parents=True,exist_ok=True)
def mix(a,b,c):
 for sr,sl,sb in ((13,8,13),(12,16,5),(3,10,15)):
  a=((a-b-c)&0xffffffff)^(c>>sr);b=(((b-c-a)&0xffffffff)^(a<<sl))&0xffffffff;c=((c-a-b)&0xffffffff)^(b>>sb)
 return a,b,c
def arrayhash(v,seed,scale):
 a=b=0x9e3779b9;c=seed;p=0
 while len(v)-p>=3:
  a,b,c=mix((a+v[p])&0xffffffff,(b+v[p+1])&0xffffffff,(c+v[p+2])&0xffffffff);p+=3
 c=(c+len(v)*scale)&0xffffffff
 if p<len(v):a=(a+v[p])&0xffffffff
 if p+1<len(v):b=(b+v[p+1])&0xffffffff
 return mix(a,b,c)[2]
class R:
 def __init__(self,b):self.b=b;self.p=0
 def take(self,n):
  if n<0 or self.p+n>len(self.b):raise ValueError('bounds')
  b=self.b[self.p:self.p+n];self.p+=n;return b
 def u8(self):return self.take(1)[0]
 def i32(self):return struct.unpack('<i',self.take(4))[0]
 def u32(self):return struct.unpack('<I',self.take(4))[0]
class Bits:
 def __init__(self,b):self.b=b;self.p=0
 def get(self,n,signed=False):
  if not 0<=n<=32 or self.p+n>len(self.b)*8:raise ValueError('bit bounds')
  v=0
  for _ in range(n):v=(v<<1)|((self.b[self.p//8]>>(7-self.p%8))&1);self.p+=1
  return v-(1<<n) if signed and n and v>>(n-1) else v
 def integer(self):
  v=0;n=0
  while True:
   if n==32:raise ValueError('nibble width')
   v|=self.get(4)<<n;n+=4
   if not self.get(1):break
  return v-(1<<n) if v>>(n-1) else v
def packet(r,decode=False,depth=0):
 if depth>8:raise ValueError('depth')
 start=r.p;n=r.i32()
 if not 0<=n<=16000000:raise ValueError('packet count')
 if not n:return [] if decode else 0
 c=r.u8()
 if c==4:
  chop=r.u8()
  if not chop:return packet(r,decode,depth+1)
  bias=r.i32();span=r.u8();a=packet(r,decode,depth+1);b=packet(r,decode,depth+1)
  if decode:return [(((x<<(span-chop))|y)+bias)&0xffffffff for x,y in zip(a,b)]
  return n
 if c==5:
  v=packet(r,decode,depth+1);offsets=packet(r,decode,depth+1)
  if decode:
   cache=[];out=[];nextvalue=0
   for offset in offsets:
    if offset==0xffffffff:
     value=v[nextvalue];nextvalue+=1
     if len(cache)==16:cache.pop()
    else:value=cache.pop(offset)
    cache.insert(0,value);out.append(value)
   if nextvalue!=len(v) or len(out)!=n:raise ValueError('MTF counts')
   return out
  return n
 nb=r.i32()
 if not 0<nb<256*1024*1024 or c not in (0,1,3):raise ValueError('codec/length')
 raw=r.take(((nb+31)//32)*4)
 if c==3:
  br=Bits(r.b[r.p:]);count=br.get(16);occ=br.get(6);val=br.get(7);minimum=br.get(32,True)
  if not count or occ>32 or val>32:raise ValueError('context')
  escape=False;entries=[]
  for _ in range(count):
   esc=bool(br.get(1));freq=br.get(occ);value=(br.get(val)+minimum)&0xffffffff
   escape|=esc;entries.append((esc,freq,value))
  r.take((br.p+7)//8)
  oob=packet(r,decode,depth+1) if escape else []
  if not decode:return n
  words=struct.unpack('<'+'I'*(len(raw)//4),raw);br=Bits(b''.join(struct.pack('>I',x) for x in words))
  low=0;high=65535;code=br.get(16);total=sum(e[1] for e in entries);a=[];oi=0
  if not 0<total<=65536:raise ValueError('arithmetic total')
  while len(a)<n:
   ran=high-low+1;target=((code-low+1)*total-1)//ran;cum=0
   for esc,freq,value in entries:
    if cum+freq>target:break
    cum+=freq
   else:raise ValueError('arithmetic symbol')
   if esc:value=oob[oi];oi+=1
   a.append(value)
   high=low+ran*(cum+freq)//total-1;low=low+ran*cum//total
   while True:
    if high<32768:pass
    elif low>=32768:low-=32768;high-=32768;code-=32768
    elif low>=16384 and high<49152:low-=16384;high-=16384;code-=16384
    else:break
    low=low*2;high=high*2+1;code=code*2+br.get(1)
  return a
 if not decode:return n
 words=struct.unpack('<'+'I'*(len(raw)//4),raw)
 if c==0:
  if len(words)!=n:raise ValueError('null count')
  return list(words)
 br=Bits(b''.join(struct.pack('>I',x) for x in words));a=[]
 if not br.get(1):
  lo=br.integer();hi=br.integer()
  if hi<lo:raise ValueError('interval')
  width=(hi-lo).bit_length()
  a=[(lo+br.get(width))&0xffffffff for _ in range(n)]
 else:
  mean=br.integer();width=0
  while len(a)<n:
   for step in range(33):
    delta=br.get(4,True);width+=delta
    if delta not in (-8,7):break
   else:raise ValueError('width changes')
   run=br.get(4)
   if not run or run>n-len(a) or not 0<=width<=32:raise ValueError('run')
   a.extend((mean+br.get(width,True))&0xffffffff for _ in range(run))
 return a
def topo(data):
 r=R(data);le=r.i32();g=str(uuid.UUID(bytes_le=r.take(16)));r.take(5)
 if g!='10dd10ab-2ac8-11d1-9b6b-0080c7bb5997':return {'other_type':g}
 r.take(2);outer=r.take(8);r.take(25);r.take(6)
 for i in range(24):
  if i==20:continue
  if i==21:
   n=r.i32();r.take(n*4)
  else:packet(r)
 r.take(4);binding=r.take(8);quant=r.take(4);nv=r.i32()
 if nv==0:return {'empty':True}
 na=r.i32();uvflags=struct.unpack_from('<I',binding,1)[0];info={'vertices':nv,'attributes':na,'binding':binding.hex(),'uv':[]}
 if binding[0]&7:
  cnt=r.i32();comp=r.u8();r.take(27)
  if cnt!=nv or comp!=3:raise ValueError('coordinate header')
  for _ in range(3):packet(r)
  r.take(4)
 if binding[0]&8:
  cnt=r.i32();comp=r.u8();bits=r.u8()
  if cnt!=na:raise ValueError('normal count')
  for _ in range(1 if bits else comp):packet(r)
  r.take(4)
 if binding[0]&48:raise ValueError('colour outside audit')
 for ch in range(8):
  flag=(uvflags>>(4*ch))&15
  if not flag:continue
  off=r.p;cnt=r.u32();comp=r.u8();bits=r.u8()
  if cnt!=na or comp not in (1,2) or flag!=(1<<(comp-1)):raise ValueError('UV header')
  if bits:
   r.take(comp*9)
   for _ in range(comp):packet(r)
   r.take(4);info['uv'].append({'channel':ch,'offset':off,'count':cnt,'bits':bits});continue
  cs=[];hashes={};allraw=[];alllag=[]
  for co in range(comp):
   po=r.p;vals=packet(r,True)
   if len(vals)!=cnt:raise ValueError('UV packet count')
   rawvals=vals.copy()
   for j in range(4,len(vals)):vals[j]=(vals[j]+vals[j-1])&0xffffffff
   allraw.append(rawvals);alllag.append(vals)
   nonfinite=[j for j,v in enumerate(vals) if (v&0x7f800000)==0x7f800000]
   cs.append({'packet_offset':po,'nonfinite_count':len(nonfinite),'first_nonfinite':nonfinite[:8],'first_words':[hex(v) for v in vals[:8]],'raw_nonfinite':sum((v&0x7f800000)==0x7f800000 for v in rawvals),'raw_first_words':[hex(v) for v in rawvals[:8]],'nan_count':sum((v&0x7f800000)==0x7f800000 and bool(v&0x7fffff) for v in vals)})
  for mode,arrays in (('raw',allraw),('lag1',alllag)):
   for scale in (1,):
    seed=0
    for arr in arrays:seed=arrayhash(arr,seed,scale)
    hashes[f'{mode}_arrays_{scale}']=seed
  h=r.u32();info['uv'].append({'channel':ch,'offset':off,'count':cnt,'bits':bits,'components':cs,'stored_hash':h,'computed_hashes':hashes})
 info['remaining_bytes']=len(data)-r.p;return info
def segments(b):
 if b[80]!=0 or not b.startswith(b'Version 10'):raise ValueError('not modern LE')
 toc=struct.unpack_from('<q',b,85)[0];n=struct.unpack_from('<i',b,toc)[0]
 for i in range(n):
  off,length,attr=struct.unpack_from('<qiI',b,toc+20+32*i)
  if off<0 or length<24 or off+length>len(b):raise ValueError('segment bounds')
  kind,size=struct.unpack_from('<ii',b,off+16)
  if kind in range(6,17):yield kind,off,b[off+24:off+length]
def container_info(b,source):
 toc=struct.unpack_from('<q',b,85)[0];n=struct.unpack_from('<i',b,toc)[0]
 counts=collections.Counter();graph=collections.Counter();refs=[]
 for i in range(n):
  off,length,attr=struct.unpack_from('<qiI',b,toc+20+32*i)
  if off<0 or length<24 or off+length>len(b):raise ValueError('segment bounds')
  kind,size=struct.unpack_from('<ii',b,off+16);counts[kind]+=1
  if kind!=1:continue
  if size!=length:raise ValueError('segment size')
  flag,encoded,algo=struct.unpack_from('<iiB',b,off+24)
  if not 1<=encoded<=length-32:raise ValueError('compressed range')
  packed=b[off+33:off+32+encoded]
  if (flag,algo)==(3,3):
   decoder=lzma.LZMADecompressor(memlimit=128*1024*1024);data=decoder.decompress(packed,max_length=128*1024*1024)
   if not decoder.eof or decoder.unused_data:raise ValueError('LZMA limit/trailing data')
  elif flag==0:data=packed
  else:raise ValueError('LSG compression outside audit')
  r=R(data)
  while r.p<len(data):
   le=r.i32();payload=r.take(le)
   if le<16:raise ValueError('graph envelope')
   guid=str(uuid.UUID(bytes_le=payload[:16]))
   if guid=='ffffffff-ffff-ffff-ffff-ffffffffffff':break
   graph[guid]+=1
   if guid=='10dd103e-2ac8-11d1-9b6b-0080c7bb5997':
    p=R(payload[21:]);p.take(5);attributes=p.i32();p.take(attributes*4)
    p.take(1);children=p.i32();p.take(children*4);p.take(5)
    chars=p.i32();ref=p.take(chars*2).decode('utf-16-le')
    if ref:
     target=(source.parent/ref).resolve()
     root_target=((ROOT if ROOT.is_dir() else ROOT.parent)/ref).resolve()
     if not target.is_file() and root_target.is_file():target=root_target
     refs.append({'local_reference':ref,'exists':target.is_file(),'self_reference':target==source.resolve()})
 return {'segment_counts':dict(counts),'graph_types':dict(graph),'partition_references':refs}
files=sorted(ROOT.rglob('*.jt')) if ROOT.is_dir() else [ROOT]
if len(files)>10000:raise ValueError('file count limit')
results=[];errors=collections.Counter();uvsegments=0
for fi,p in enumerate(files):
 if p.stat().st_size>128*1024*1024:raise ValueError('file byte limit')
 b=p.read_bytes();row={'id':f'PA-{fi:03d}','local_path':str(p),'sha256':hashlib.sha256(b).hexdigest(),'bytes':len(b),'version':b[:80].decode('ascii',errors='replace').strip(),'shapes':[]}
 try:
  if len(b)<109 or b[80]!=0 or not b.startswith(b'Version 10'):raise ValueError('not modern LE')
  row['container']=container_info(b,p)
  for kind,off,data in segments(b):
   try:
    shape=topo(data);shape.update({'segment_type':kind,'file_offset':off});row['shapes'].append(shape)
    if shape.get('uv'):uvsegments+=1
   except Exception as e:
    errors[str(e)]+=1;row['shapes'].append({'segment_type':kind,'file_offset':off,'audit_error':str(e)})
 except Exception as e:row['audit_error']=str(e);errors[str(e)]+=1
 results.append(row)
report={'files':results,'audit_errors':dict(errors),'uv_segments':uvsegments,'qualification':'Read-only partial payload audit; not native importer execution or full geometry validation.'}
OUT.write_text(json.dumps(report,indent=2))
print('files',len(files),'UV segments',uvsegments,'errors',dict(errors))
for row in results:
 bad=[s for s in row['shapes'] if any(c.get('nonfinite_count') for uv in s.get('uv',[]) for c in uv.get('components',[]))]
 if bad:
  print(row['id'],row['sha256'],'affected LODs',len(bad))
  for s in bad:
   for uv in s['uv']:print(s['segment_type'],'matching hash modes',[k for k,v in uv['computed_hashes'].items() if v==uv['stored_hash']],'raw nonfinite',[c['raw_nonfinite'] for c in uv['components']])
