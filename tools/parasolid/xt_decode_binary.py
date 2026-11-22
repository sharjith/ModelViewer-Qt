#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Reference decoder for neutral-binary XT data (as carried by JT XT B-Rep elements).

Decodes the first 'P' 'S' 0 0 stream found in FILE with the layouts in LAYOUTS.txt (the same
text format as the C++ reader; the tracked JT table can be extracted from
src/Import/Parasolid/XtJtLayouts.cpp). Prints schema key, record counts, whether the
terminator was reached and where. Builds no geometry. Research tool; the C++ reader
(src/Import/Parasolid/XtReader.cpp) must agree with it.

    python -I tools/parasolid/xt_decode_binary.py LAYOUTS.txt FILE
"""
import struct, sys, json, collections
lay={}
for l in open(sys.argv[1]):
    if l.startswith('#') or not l.strip(): continue
    p=l.split(); fs=[]
    for it in p[2:]:
        n,t=it.split(':'); ne=0
        if t.endswith('[]'): t=t[0]; ne=1
        elif '[' in t: t,k=t[:-1].split('['); ne=int(k)
        fs.append((n,t,ne))
    lay[int(p[0])]=(p[1],fs)
d=open(sys.argv[2],'rb').read(); off=d.index(b'PS\0\0')
class R:
    def __init__(s,b,p): s.b,s.p=b,p
    def u8(s): v=s.b[s.p]; s.p+=1; return v
    def i16(s): v=struct.unpack_from('>h',s.b,s.p)[0]; s.p+=2; return v
    def i32(s): v=struct.unpack_from('>i',s.b,s.p)[0]; s.p+=4; return v
    def f64(s): v=struct.unpack_from('>d',s.b,s.p)[0]; s.p+=8; return v
    def idx(s):
        r=s.i16(); q=0
        if r<0: q=s.i16(); r=-r
        return q*32767+r-1
    def s8(s): n=s.u8(); v=s.b[s.p:s.p+n]; s.p+=n; return v.decode('latin-1')
r=R(d,off+4)
n=r.i16(); mod=bytes(d[r.p:r.p+n]); r.p+=n
n=r.i32(); sch=bytes(d[r.p:r.p+n]).decode(); r.p+=n
print(mod, sch)
emb=sch.count('_')==3
lim=r.i16() if emb else None
usf=r.i32(); print('limit',lim,'usf',usf)
def fdef(r):
    name=r.s8(); pc=r.i16(); ne=r.idx()
    ty='p' if pc else r.s8()
    if not pc and ne==1: r.u8()
    return (name,ty,ne)
def val(r,t):
    if t in 'p': return r.idx()
    if t=='d': return r.i32()
    if t in 'nw': return r.i16()
    if t in 'ucl': return r.u8()
    if t=='f': return r.f64()
    if t=='v': return [r.f64() for _ in range(3)]
    if t=='h': return [r.f64() for _ in range(3)]
    if t=='i': return [r.f64() for _ in range(2)]
    if t=='b': return [r.f64() for _ in range(6)]
L={}; cnt=collections.Counter(); nodes=0
try:
    while True:
        t=r.i16()
        if t==1:
            i=r.idx(); print('terminator idx',i,'pos',r.p,'of',len(d)); break
        if t not in L:
            if emb:
                flag=d[r.p]
                if flag==255: r.p+=1; L[t]=lay[t]
                else:
                    fc=r.u8()
                    if t in lay:
                        base=lay[t][1]; cur=[]; i=0
                        while True:
                            op=chr(r.u8())
                            if op=='C': cur.append(base[i]); i+=1
                            elif op=='D': i+=1
                            elif op in 'IA': cur.append(fdef(r))
                            elif op=='Z': break
                            else: raise Exception('op %r'%op)
                        assert i==len(base),(t,i,len(base)); assert len(cur)==fc
                        L[t]=(lay[t][0],cur)
                    else:
                        nm=r.s8(); r.s8(); L[t]=(nm,[fdef(r) for _ in range(fc)])
            else: L[t]=lay[t]
        name,fs=L[t]
        var=fs and fs[-1][2]==1
        nv=r.i32() if var else 0
        idx=r.idx()
        for fn,ft,ne in fs:
            k=1 if ne==0 else (nv if ne==1 else ne)
            for _ in range(k): val(r,ft)
        cnt[name]+=1; nodes+=1
except Exception as e:
    print('STOP',nodes,repr(e),'pos',r.p)
print(nodes,dict(cnt))
