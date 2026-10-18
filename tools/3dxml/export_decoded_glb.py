"""Owner-inspection GLB: original XML hierarchy, shared meshes, face colors.

Uses only the existing sample-derived decoder. No application changes.
Geometry stays in local coordinates; root converts inferred mm/native Z-up
to glTF metres/Y-up. Flat normals are derived from stored triangle winding.
"""
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import struct
import tempfile
import zipfile
import xml.etree.ElementTree as ET

import numpy as np


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('--occurrences', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    api = runpy.run_path(str(Path(__file__).with_name('decode_representation.py')))
    scene = json.loads(args.occurrences.read_text(encoding='utf-8'))
    doc = dict(asset=dict(version='2.0', generator='ModelViewer sample research exporter'),
               scene=0, scenes=[dict(nodes=[0])], nodes=[], meshes=[], materials=[],
               accessors=[], bufferViews=[], buffers=[],
               extensionsUsed=['KHR_node_visibility'])
    basis = np.array([[.001, 0, 0, 0], [0, 0, .001, 0],
                      [0, -.001, 0, 0], [0, 0, 0, 1]], dtype=np.float64)
    def column_matrix(rows):
        return np.asarray(rows).T.reshape(-1).tolist()
    palette, rep_meshes, mesh_triangles, audit = {}, {}, {}, []
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryFile() as binary, zipfile.ZipFile(args.archive) as archive:
        def accessor(values):
            values = np.asarray(values, dtype='<f4').reshape(-1, 3)
            view = len(doc['bufferViews'])
            doc['bufferViews'].append(dict(buffer=0, byteOffset=binary.tell(),
                                          byteLength=values.nbytes, target=34962))
            binary.write(values.tobytes())
            index = len(doc['accessors'])
            doc['accessors'].append(dict(bufferView=view, componentType=5126,
                                        count=len(values), type='VEC3',
                                        min=values.min(axis=0).astype(float).tolist(),
                                        max=values.max(axis=0).astype(float).tolist()))
            return index
        def material(rgb):
            if rgb not in palette:
                palette[rgb] = len(doc['materials'])
                # Match the owner-verified OBJ numeric diffuse values. Source
                # color-space semantics remain unproven; do not impose gamma.
                values = np.asarray(rgb, dtype=float) / 255
                doc['materials'].append(dict(name='face_rgb_' + bytes(rgb).hex(),
                    pbrMetallicRoughness=dict(baseColorFactor=values.tolist()+[1.0],
                                             metallicFactor=0.0, roughnessFactor=float(np.sqrt(2/26))),
                    extras=dict(sourceRGB=list(rgb))))
            return palette[rgb]
        occurrences = scene['occurrences']
        members = sorted({rep['associatedFile'].removeprefix('urn:3DXML:')
                          for item in occurrences if item['visible'] for rep in item['representations']
                          if rep['format'] == 'UVR'})
        for member in members:
            data = archive.read(member)
            entries = api['compound_streams'](data)
            skeleton = next(e for e in entries if e['name'] == 'Skeleton')
            resources = {}
            def collect(leaf, mesh, groups, styles):
                resources[leaf['offset']] = (mesh, styles)
            graph = api['skeleton_graph'](api['join_stream'](data, skeleton), collect)
            api['surface_links'](data, entries, graph)
            nodes = {node['offset']: node for node in graph['nodes']}
            def instances(offset=0, path=(), ancestors=frozenset()):
                if offset in ancestors:
                    raise ValueError('cyclic internal graph')
                if offset in resources:
                    yield offset, path
                else:
                    for i, link in enumerate(nodes[offset]['child_links']):
                        yield from instances(link['target'], path+(i,), ancestors | {offset})
            primitive_resources = {}
            for offset, (mesh, styles) in resources.items():
                points = np.asarray(mesh['positions_serialized'], dtype=np.float64)
                colors = {}
                assert len(styles) == len(mesh['finest_faces'])
                for face, bgr in zip(mesh['finest_faces'], styles):
                    colors.setdefault(tuple(reversed(bgr)), []).extend(face['triangles'])
                primitives = []
                for rgb, triangles in colors.items():
                    if not triangles:
                        continue
                    indices = np.asarray(triangles, dtype=np.int64)
                    positions = points[indices]
                    normals = np.cross(positions[:, 1]-positions[:, 0],
                                       positions[:, 2]-positions[:, 0])
                    lengths = np.linalg.norm(normals, axis=1)
                    assert np.all(lengths > 0) and np.all(np.isfinite(positions))
                    normals /= lengths[:, None]
                    primitives.append(dict(attributes=dict(POSITION=accessor(positions),
                        NORMAL=accessor(np.repeat(normals, 3, axis=0))),
                        material=material(rgb), mode=4,
                        extras=dict(sourceLeafOffset=offset, triangles=len(indices))))
                primitive_resources[offset] = primitives
            body_meshes = []
            for offset, path in instances():
                primitives = []
                for primitive in primitive_resources[offset]:
                    primitive = dict(primitive)
                    primitive['extras'] = dict(primitive['extras'], sourceBodyPath=list(path))
                    primitives.append(primitive)
                if primitives:
                    body_meshes.append(len(doc['meshes']))
                    doc['meshes'].append(dict(name=Path(member).stem+'__body_'+('_'.join(map(str,path)) or '0'),
                        primitives=primitives, extras=dict(sourceMember=member, sourceBodyPath=list(path))))
            rep_meshes[member] = body_meshes
            count = sum(p['extras']['triangles'] for index in body_meshes for p in doc['meshes'][index]['primitives'])
            mesh_triangles[member] = count
            audit.append(dict(member=member, triangles=count, bodies=len(list(instances()))))
            print(member, count, 'triangles', flush=True)
        local = lambda e: e.tag.rsplit('}', 1)[-1]
        manifest = ET.fromstring(archive.read('Manifest.xml'))
        root_member = next(e.text.strip() for e in manifest.iter() if local(e) == 'Root')
        product = ET.fromstring(archive.read(root_member))
        root_name = next(e.attrib.get('name', 'ASM-GETRAG_V160') for e in product.iter()
                         if local(e) == 'Reference3D' and e.attrib['id'] == scene['root_reference'])
        doc['nodes'].append(dict(name=root_name, matrix=column_matrix(basis),
                                extras=dict(sourceReference=scene['root_reference'])))
        paths = {(): 0}
        for item in occurrences:
            paths[tuple(item['path'])] = len(doc['nodes'])
            node = dict(name=item['name'], matrix=column_matrix(item['local_matrix_rows']),
                        extras=dict(sourcePath=item['path'], sourceReference=item['reference_id']))
            if not item['visible']:
                node['extensions'] = dict(KHR_node_visibility=dict(visible=False))
            reps = [rep['associatedFile'].removeprefix('urn:3DXML:')
                    for rep in item['representations'] if rep['format'] == 'UVR']
            assert len(reps) <= 1, 'multiple representations require additional child nodes'
            if reps and item['visible'] and len(rep_meshes.get(reps[0], [])) == 1:
                node['mesh'] = rep_meshes[reps[0]][0]
            doc['nodes'].append(node)
        for item in occurrences:
            path = tuple(item['path'])
            doc['nodes'][paths[path[:-1]]].setdefault('children', []).append(paths[path])
        for item in occurrences:
            if not item['visible']:
                continue
            for rep in item['representations']:
                members_meshes = rep_meshes.get(rep['associatedFile'].removeprefix('urn:3DXML:'), [])
                if len(members_meshes) > 1:
                    for mesh_index in members_meshes:
                        body = doc['meshes'][mesh_index]
                        node_index = len(doc['nodes'])
                        doc['nodes'].append(dict(name=body['name'], mesh=mesh_index,
                            matrix=column_matrix(np.eye(4)), extras=dict(sourceBodyPath=body['extras']['sourceBodyPath'])))
                        doc['nodes'][paths[tuple(item['path'])]].setdefault('children', []).append(node_index)
        size = binary.tell()
        doc['buffers'] = [dict(byteLength=size)]
        json_bytes = json.dumps(doc, separators=(',', ':'), ensure_ascii=True).encode('utf-8')
        json_bytes += b' ' * (-len(json_bytes) % 4)
        binary_padding = b'\0' * (-size % 4)
        with args.output.open('wb') as output:
            output.write(struct.pack('<4sII', b'glTF', 2, 12+8+len(json_bytes)+8+size+len(binary_padding)))
            output.write(struct.pack('<I4s', len(json_bytes), b'JSON'))
            output.write(json_bytes)
            output.write(struct.pack('<I4s', size+len(binary_padding), b'BIN\0'))
            binary.seek(0)
            while chunk := binary.read(8*1024*1024):
                output.write(chunk)
            output.write(binary_padding)
    visible_triangles = sum(mesh_triangles[rep['associatedFile'].removeprefix('urn:3DXML:')]
                            for item in occurrences if item['visible']
                            for rep in item['representations'] if rep['format'] == 'UVR')
    with args.output.open('rb') as output:
        digest = hashlib.file_digest(output, 'sha256').hexdigest()
    report = dict(glb=args.output.name, sha256=digest, bytes=args.output.stat().st_size,
                  xml_nodes=len(occurrences)+1, total_nodes=len(doc['nodes']), hidden_occurrences=sum(not i['visible'] for i in occurrences),
                  mesh_resources=len(doc['meshes']), materials=len(palette),
                  visible_triangles=visible_triangles, representations=audit,
                  units='metres; source millimetres inferred',
                  normal_policy='derived flat normals',
                  color_policy='BGR face palette to RGB/255; same numeric diffuse and derived roughness as verified OBJ',
                  visibility='hidden XML nodes retained without geometry; no visibility-extension dependency for display')
    args.output.with_suffix('.manifest.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k != 'representations'}, indent=2))


if __name__ == '__main__':
    main()
