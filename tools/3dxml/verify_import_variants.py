"""Audit provided 3DXML variants against the native importer without exporting geometry."""
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import struct
import subprocess
import tempfile
import xml.etree.ElementTree as ET
import zipfile

DECODER = runpy.run_path(str(Path(__file__).with_name('decode_representation.py')))

def local(e):
    return e.tag.rsplit('}', 1)[-1]

def nondegenerate(points, triangle):
    a, b, c = (points[i] for i in triangle)
    u = [b[k]-a[k] for k in range(3)]
    v = [c[k]-a[k] for k in range(3)]
    cross = [u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]]
    return any(x != 0 for x in cross)

def xml_counts(data):
    root = ET.fromstring(data)
    raw, rendered, bodies = 0, 0, 0
    for rep in root.iter():
        if rep.attrib.get('{http://www.w3.org/2001/XMLSchema-instance}type') != 'PolygonalRepType':
            continue
        bodies += 1
        buffer = next(e for e in rep if local(e) == 'VertexBuffer')
        text = next(e.text for e in buffer if local(e) == 'Positions')
        values = [struct.unpack('<f', struct.pack('<f', float(v)))[0] for v in text.replace(',', ' ').split()]
        points = list(zip(values[::3], values[1::3], values[2::3]))
        def visit(e):
            nonlocal raw, rendered
            if local(e) in ('PolygonalLOD', 'Rep'):
                return
            if local(e) == 'Face':
                for kind in ('triangles', 'strips', 'fans'):
                    for group in e.attrib.get(kind, '').split(','):
                        indices = [int(v) for v in group.split()]
                        if not indices:
                            continue
                        if kind == 'triangles':
                            assert len(indices) % 3 == 0
                            triangles = zip(indices[::3], indices[1::3], indices[2::3])
                        elif kind == 'fans':
                            triangles = [(indices[0], indices[i-1], indices[i]) for i in range(2,len(indices))]
                        else:
                            triangles = [(indices[i-2], indices[i-1], indices[i]) for i in range(2,len(indices))]
                        for triangle in triangles:
                            raw += 1
                            rendered += nondegenerate(points,triangle)
            for c in e:
                visit(c)
        for e in rep:
            visit(e)
    return bodies, raw, rendered

def source_counts(path):
    counts = dict(mesh_bodies=0, source_triangles=0, renderable_triangles=0,
                  uvr_members=0, xml_members=0, annotation_members=0, review_members=0)
    with zipfile.ZipFile(path) as archive:
        manifest = ET.fromstring(archive.read('Manifest.xml'))
        root = next(e.text.strip() for e in manifest.iter() if local(e)=='Root')
        reps = {e.attrib['associatedFile'].removeprefix('urn:3DXML:'):e.attrib['format']
                for e in ET.fromstring(archive.read(root)).iter() if local(e)=='ReferenceRep'}
        for member, fmt in reps.items():
            if fmt=='TECHREP':
                counts['review_members'] += 1
                continue
            data = archive.read(member)
            if fmt=='TESSELLATED':
                bodies, raw, rendered = xml_counts(data)
                counts['xml_members'] += 1
                counts['mesh_bodies'] += bodies
                counts['source_triangles'] += raw
                counts['renderable_triangles'] += rendered
                continue
            assert fmt=='UVR', fmt
            entries = DECODER['compound_streams'](data)
            skeletons = [e for e in entries if e['name']=='Skeleton' and e['kind']==2]
            if not skeletons:
                assert not any(e['name']=='SurfacicReps' for e in entries)
                assert any(e['name']=='V4V5_FDT' and e['kind']==1 for e in entries)
                counts['annotation_members'] += 1
                continue
            counts['uvr_members'] += 1
            def visit(leaf, mesh, groups, styles):
                counts['mesh_bodies'] += 1
                points = mesh['positions_serialized']
                for face in mesh['finest_faces']:
                    for triangle in face['triangles']:
                        counts['source_triangles'] += 1
                        counts['renderable_triangles'] += nondegenerate(points, triangle)
            graph = DECODER['skeleton_graph'](DECODER['join_stream'](data,skeletons[0]),visit)
            DECODER['surface_links'](data,entries,graph)
    counts['collapsed_triangles'] = counts['source_triangles']-counts['renderable_triangles']
    return counts

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('samples',type=Path)
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args()
    rows=[]
    paths=sorted(args.samples.glob("*.3dxml"))
    if not paths:
        raise ValueError("No 3DXML archives found")
    with tempfile.TemporaryDirectory() as temporary:
        for path in paths:
            source=source_counts(path)
            report=Path(temporary)/'native.json'
            report.unlink(missing_ok=True)
            result=subprocess.run([str(args.executable.resolve()),'--audit',str(path),str(report)],capture_output=True,text=True)
            if result.returncode:
                raise RuntimeError(f'{path.name}: {result.stdout}\n{result.stderr}')
            native=json.loads(report.read_text())
            assert native['source_sha256']==hashlib.sha256(path.read_bytes()).hexdigest()
            assert native['mesh_occurrences']>0 and native['visible_triangles']>0
            assert native['resource_triangles']==source['renderable_triangles'], (path.name,source,native)
            native.pop('source',None)
            rows.append(dict(file=path.name,independent=source,native=native))
            print(path.name,source,'PASS',flush=True)
    args.report.write_text(json.dumps(dict(passed=True,archives=rows),indent=2)+'\n')

if __name__=='__main__':
    main()
