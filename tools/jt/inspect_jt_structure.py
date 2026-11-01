#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Read-only JT container/LSG inventory, without geometry export or model strings.
Uses only Python's standard library. Does not follow external files or validate
geometry. Outputs standard type GUIDs/counts, version/size and optional file hash.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
from datetime import datetime
import uuid
import zlib

LIMIT = 128*1024*1024
END = 'ffffffff-ffff-ffff-ffff-ffffffffffff'
SABOT = '96603dd3-5a0f-40f5-97cb-1e9647cbd37e'


def repo_root():
    return next((p for p in Path(__file__).resolve().parents if (p/'src/ThirdParty/occt-jt').is_dir() and (p/'AGENTS.md').is_file()),None)


def known_types():
    bundled=Path(getattr(sys,'_MEIPASS',Path(__file__).parent))/'jt_type_registry.json'
    if bundled.is_file():return json.loads(bundled.read_text(encoding='utf-8'))['types']
    repo=repo_root()
    if repo is None:return {}
    root = repo/'src/ThirdParty/occt-jt'
    result = {}
    pattern = re.compile(r'IMPLEMENT_OBJECT_CLASS\s*\(\s*(\w+)\s*,\s*"[^"]*"\s*,\s*"([\da-fA-F-]+)"', re.S)
    for path in root.rglob('*.cxx'):
        for name, value in pattern.findall(path.read_text(encoding='utf-8', errors='replace')):
            result[str(uuid.UUID(hex=value.replace('-', '')))] = name
    return result


def inventory(path, include_hash=True, report=None):
    if report is None:report={}
    report.update({'tool_schema':1,'geometry_exported':False,'model_strings_exported':False,'external_files_followed':False})
    registry = known_types(); report['registry_available'] = bool(registry)
    with path.open('rb') as stream:
        size = stream.seek(0,2); stream.seek(0); header = stream.read(109)
        report['bytes'] = size
        if include_hash:
            stream.seek(0); digest = hashlib.sha256()
            while block := stream.read(1024*1024):digest.update(block)
            report['sha256'] = digest.hexdigest()
        match = re.match(rb'Version\s+(\d+)\.(\d+)',header[:80])
        if not match or len(header)<105:raise ValueError('Not a complete JT header')
        major, minor = map(int,match.groups()); report['version'] = f'{major}.{minor}'
        if major<8 or major>10:raise ValueError('Inventory header family is limited to 8.x/9.x/10.x')
        if header[80] not in (0,1):raise ValueError('Invalid byte-order marker')
        big = header[80]==1; endian = '>' if big else '<'; report['byte_order'] = 'BE' if big else 'LE'
        def read_at(offset,count):
            if offset<0 or count<0 or offset+count>size:raise ValueError('Container range is outside the file')
            stream.seek(offset); data = stream.read(count)
            if len(data)!=count:raise ValueError('Container read was truncated')
            return data
        def guid(data):return str(uuid.UUID(bytes=data)) if big else str(uuid.UUID(bytes_le=data))
        offset_width = 8 if major>=10 else 4
        toc = struct.unpack_from(endian+('q' if major>=10 else 'i'),header,85)[0]
        lsg_id = guid(header[85+offset_width:101+offset_width])
        count = struct.unpack(endian+'i',read_at(toc,4))[0]; entry_width = 24+offset_width
        if count<1 or count>1000000 or toc+4+count*entry_width>size:raise ValueError('Invalid TOC count/range')
        report['toc_entries'] = count; segments = Counter(); lsg = None; seen = set()
        for index in range(count):
            entry = read_at(toc+4+index*entry_width,entry_width); ident = guid(entry[:16])
            offset,length,attributes = struct.unpack_from(endian+('qiI' if major>=10 else 'iiI'),entry,16)
            if ident in seen or offset<0 or length<24 or offset+length>size:raise ValueError('Invalid/duplicate TOC entry')
            seen.add(ident); segments[(attributes>>24)&255] += 1
            if ident==lsg_id:lsg=(offset,length)
        report['segment_counts'] = {str(k):v for k,v in sorted(segments.items())}
        report['shape_lod_segments_6_to_16'] = sum(segments[x] for x in range(6,17))
        report['xt_segments_17_or_30'] = segments[17]+segments[30]
        report['jt_brep_segments_2'] = segments[2]
        if lsg is None:raise ValueError('Root LSG segment is absent from TOC')
        offset,toc_length = lsg; seg = read_at(offset,33)
        kind,length,flag,encoded_length,algorithm = struct.unpack_from(endian+'iiiiB',seg,16)
        if kind!=1 or length<33 or length>toc_length:raise ValueError('Invalid root LSG segment header')
        report['lsg_compression'] = {'flag':flag,'algorithm':algorithm}
        if encoded_length<1 or encoded_length>length-32:raise ValueError('Invalid root LSG payload length')
        if encoded_length-1>LIMIT:
            report['lsg_inventory'] = 'not scanned: compressed input limit'; return report
        packed = read_at(offset+33,encoded_length-1)
        if flag==0:data=packed
        elif flag==2 and algorithm==2:
            decoder = zlib.decompressobj();data=decoder.decompress(packed,LIMIT+1)
            if len(data)>LIMIT or decoder.unconsumed_tail:
                report['lsg_inventory']='not scanned: decompressed byte limit';return report
            if not decoder.eof:raise ValueError('Truncated zlib LSG payload')
        else:
            report['lsg_inventory']='not scanned: compression profile outside this diagnostic tool';return report
        report['lsg_bytes'] = len(data); pos = 0; section = 0; counters = [Counter(),Counter()]; wrappers = 0
        while section<2:
            if pos+4>len(data):raise ValueError('Truncated graph element length')
            length = struct.unpack_from(endian+'i',data,pos)[0];start=pos+4;end=start+length
            if length<16 or end>len(data):raise ValueError('Invalid graph element envelope')
            value = guid(data[start:start+16]);pos=end
            if value==END:
                if length!=16:raise ValueError('Invalid graph end marker')
                section+=1;continue
            if length<21:raise ValueError('Truncated graph element header')
            if value==SABOT:
                if major<10 or length<42:raise ValueError('Invalid Sabot wrapper envelope')
                wrappers+=1;value=guid(data[start+21:start+37])
            counters[section][value]+=1
        def rows(counter):
            return [{'type_guid':value,'count':number,'registered_class':registry.get(value)} for value,number in sorted(counter.items())]
        report['graph_types']=rows(counters[0]);report['property_types']=rows(counters[1]);report['sabot_wrappers']=wrappers
        report['lsg_inventory']='complete element-header inventory; payloads/geometry not validated'
    return report


def report_for(path, include_hash=True):
    report={}
    try:inventory(path,include_hash,report)
    except OSError as error:report.update({'error':'File access failed','errno':error.errno})
    except (ValueError,struct.error,zlib.error) as error:report['error']=str(error)
    return report


def graphical_main():
    import tkinter as tk
    from tkinter import filedialog,messagebox
    root=tk.Tk();root.withdraw()
    try:
        files=filedialog.askopenfilenames(parent=root,title='Select JT files to inspect',filetypes=[('JT models','*.jt'),('All files','*.*')])
        if not files:return
        folder=filedialog.askdirectory(parent=root,title='Choose a folder for the diagnostic reports',mustexist=True)
        if not folder:return
        stamp=datetime.now().strftime('%Y%m%d-%H%M%S-%f')
        for index,value in enumerate(files,1):
            target=Path(folder)/f'JT-report-{stamp}-{index:02d}.json'
            target.write_text(json.dumps(report_for(Path(value)),indent=2)+'\n',encoding='utf-8')
        messagebox.showinfo('JT Diagnostics',f'Saved {len(files)} diagnostic report(s) in the selected folder.\n\nThe JT files were not modified. Reports contain no geometry or model names.',parent=root)
    except Exception:
        messagebox.showerror('JT Diagnostics','The diagnostic operation could not finish. Check that the files are accessible and the selected report folder is writable.',parent=root)
    finally:root.destroy()


def main():
    if len(sys.argv)==1:
        graphical_main();return
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('input',type=Path)
    parser.add_argument('--output',type=Path,required=True);parser.add_argument('--no-hash',action='store_true')
    args=parser.parse_args()
    if args.output.resolve()==args.input.resolve():parser.error('Output must differ from the input file')
    repo=repo_root();output=args.output.resolve()
    if repo is not None and output.is_relative_to(repo) and not output.is_relative_to(repo/'out'):
        parser.error('Keep repository-local reports under ignored out/')
    report=report_for(args.input,not args.no_hash)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('Wrote structural inventory; input model was not modified or exported.')


if __name__=='__main__':main()
