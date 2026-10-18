"""Independently check GLB hierarchy, placements, geometry, and OBJ world bounds."""
import argparse
from collections import Counter
import json
from pathlib import Path
import struct

import numpy as np


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('glb', type=Path)
    parser.add_argument('--occurrences', type=Path, required=True)
    parser.add_argument('--obj-manifest', type=Path, required=True)
    args = parser.parse_args()
    with args.glb.open('rb') as source:
        magic, version, total = struct.unpack('<4sII', source.read(12))
        assert magic == b'glTF' and version == 2 and total == args.glb.stat().st_size
        length, kind = struct.unpack('<I4s', source.read(8))
        assert kind == b'JSON' and length % 4 == 0
        doc = json.loads(source.read(length))
        length, kind = struct.unpack('<I4s', source.read(8))
        assert kind == b'BIN\0' and length % 4 == 0
        binary_start = source.tell()
        assert binary_start + length == total
    def values(index):
        accessor = doc['accessors'][index]
        view = doc['bufferViews'][accessor['bufferView']]
        assert accessor['componentType'] == 5126 and accessor['type'] == 'VEC3'
        assert view['byteLength'] == accessor['count'] * 12
        assert view['byteOffset'] % 4 == 0
        assert view['byteOffset'] + view['byteLength'] <= doc['buffers'][0]['byteLength']
        array = np.memmap(args.glb, dtype='<f4', mode='r',
                          offset=binary_start+view['byteOffset'], shape=(accessor['count'], 3))
        assert np.all(np.isfinite(array))
        assert np.array_equal(array.min(axis=0), accessor['min'])
        assert np.array_equal(array.max(axis=0), accessor['max'])
        return array
    resource_triangles = []
    for mesh in doc['meshes']:
        count = 0
        for primitive in mesh['primitives']:
            assert primitive['mode'] == 4 and 'indices' not in primitive
            p = values(primitive['attributes']['POSITION'])
            n = values(primitive['attributes']['NORMAL'])
            assert len(p) == len(n) and len(p) % 3 == 0
            triangle = p.astype(np.float64).reshape(-1, 3, 3)
            cross = np.cross(triangle[:, 1]-triangle[:, 0], triangle[:, 2]-triangle[:, 0])
            lengths = np.linalg.norm(cross, axis=1)
            assert np.all(lengths > 0)
            expected = np.repeat(cross / lengths[:, None], 3, axis=0)
            assert np.max(np.abs(expected-n)) < 1e-6
            assert 0 <= primitive['material'] < len(doc['materials'])
            count += len(p)//3
        resource_triangles.append(count)
    fixture = json.loads(args.occurrences.read_text(encoding='utf-8'))
    baseline = json.loads(args.obj_manifest.read_text(encoding='utf-8'))
    obj_bodies = {(tuple(o['occurrence_path']), o['member'], tuple(o['body_path'])): o
                  for o in baseline['objects']}
    matched_bodies = set()
    expected_nodes = {tuple(item['path']): item for item in fixture['occurrences']}
    assert sum('sourcePath' in n.get('extras', {}) for n in doc['nodes']) == len(expected_nodes)
    root_basis = np.array([[.001, 0, 0, 0], [0, 0, .001, 0],
                           [0, -.001, 0, 0], [0, 0, 0, 1]])
    seen, paths, world = set(), {}, {}
    low, high = np.full(3, np.inf), np.full(3, -np.inf)
    visible_triangles, hidden = 0, 0
    def visit(index, parent_world, parent_path, parent_visible):
        nonlocal low, high, visible_triangles, hidden
        assert index not in seen
        seen.add(index)
        node = doc['nodes'][index]
        local = np.asarray(node['matrix']).reshape(4, 4).T
        placement = parent_world @ local
        visible = parent_visible and node.get('extensions', {}).get(
            'KHR_node_visibility', {}).get('visible', True)
        if index == 0:
            path = ()
            assert np.array_equal(local, root_basis)
        elif 'sourcePath' in node.get('extras', {}):
            path = tuple(node['extras']['sourcePath'])
            item = expected_nodes[path]
            assert path[:-1] == parent_path and node['name'] == item['name']
            assert np.array_equal(local, item['local_matrix_rows'])
            assert np.allclose(placement, root_basis @ item['world_matrix_rows'], atol=1e-12, rtol=0)
            assert visible == item['visible']
            hidden += not visible
            if not visible:
                assert 'mesh' not in node, 'hidden helpers must not depend on reader visibility support'
        else:
            assert 'sourceBodyPath' in node['extras']
            assert np.array_equal(local, np.eye(4)) and visible
            path = parent_path
        paths[path] = index
        world[index] = placement
        if visible and 'mesh' in node:
            mesh = doc['meshes'][node['mesh']]
            key = (path, mesh['extras']['sourceMember'], tuple(mesh['extras']['sourceBodyPath']))
            assert key not in matched_bodies
            matched_bodies.add(key)
            expected_colors = Counter()
            for run in obj_bodies[key]['face_materials']:
                expected_colors[run['material']] += run['triangles']
            actual_colors = Counter()
            for primitive in mesh['primitives']:
                actual_colors[doc['materials'][primitive['material']]['name']] += (
                    doc['accessors'][primitive['attributes']['POSITION']]['count']//3)
            assert actual_colors == expected_colors
            visible_triangles += resource_triangles[node['mesh']]
            for primitive in mesh['primitives']:
                points = values(primitive['attributes']['POSITION']).astype(np.float64)
                points = points @ placement[:3, :3].T + placement[:3, 3]
                low = np.minimum(low, points.min(axis=0))
                high = np.maximum(high, points.max(axis=0))
        for child in node.get('children', []):
            visit(child, placement, path, visible)
    assert doc['scenes'][doc['scene']]['nodes'] == [0]
    visit(0, np.eye(4), (), True)
    assert len(seen) == len(doc['nodes'])
    assert matched_bodies == set(obj_bodies)
    expected_low = np.min([o['bounds_min'] for o in baseline['objects']], axis=0)*.001
    expected_high = np.max([o['bounds_max'] for o in baseline['objects']], axis=0)*.001
    assert np.allclose(low, expected_low, atol=1e-10, rtol=0)
    assert np.allclose(high, expected_high, atol=1e-10, rtol=0)
    assert visible_triangles == baseline['triangles']
    # A source representation must remain a shared resource for repeated nodes.
    bindings = {}
    for node in doc['nodes']:
        if 'mesh' in node:
            member = doc['meshes'][node['mesh']]['extras']['sourceMember']
            body = tuple(doc['meshes'][node['mesh']]['extras']['sourceBodyPath'])
            assert bindings.setdefault((member, body), node['mesh']) == node['mesh']
    obj_splits = sum(len({run['material'] for run in o['face_materials'] if run['triangles']})
                     for o in baseline['objects'])
    primitive_occurrences = sum(len(doc['meshes'][n['mesh']]['primitives'])
                                for n in doc['nodes'] if 'mesh' in n)
    assert primitive_occurrences == obj_splits
    for material in doc['materials']:
        rgb = baseline['face_color_palette'][material['name']]
        pbr = material['pbrMetallicRoughness']
        assert np.allclose(pbr['baseColorFactor'][:3], np.array(rgb)/255, atol=1e-12, rtol=0)
        assert abs(pbr['roughnessFactor']-np.sqrt(2/26)) < 1e-12
    report = dict(passed=True, nodes=len(seen), hidden_occurrences=hidden,
                  mesh_resources=len(doc['meshes']), materials=len(doc['materials']),
                  resource_triangles=sum(resource_triangles), visible_triangles=visible_triangles,
                  primitive_occurrences=primitive_occurrences, obj_body_color_splits=obj_splits,
                  bounds_metres_min=low.tolist(), bounds_metres_max=high.tolist(),
                  checks=['all XML parent links, names and local/world transforms',
                          'all accessor ranges, finite values and bounds',
                          'every triangle normal and winding', 'shared representation bindings',
                          'hidden helper geometry omitted', 'palette and roughness match OBJ numeric values',
                          'body/color split counts match OBJ',
                          'every occurrence/body/color triangle count matches OBJ',
                          'visible triangle count and world bounds match verified OBJ'])
    args.glb.with_suffix('.verification.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
