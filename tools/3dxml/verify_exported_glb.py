"""Independently check GLB hierarchy, placements, geometry, and OBJ world bounds."""
import argparse
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
    expected_nodes = {tuple(item['path']): item for item in fixture['occurrences']}
    assert len(doc['nodes']) == len(expected_nodes)+1
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
        else:
            path = tuple(node['extras']['sourcePath'])
            item = expected_nodes[path]
            assert path[:-1] == parent_path and node['name'] == item['name']
            assert np.array_equal(local, item['local_matrix_rows'])
            assert np.allclose(placement, root_basis @ item['world_matrix_rows'], atol=1e-12, rtol=0)
            assert visible == item['visible']
            hidden += not visible
        paths[path] = index
        world[index] = placement
        if visible and 'mesh' in node:
            mesh = doc['meshes'][node['mesh']]
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
    baseline = json.loads(args.obj_manifest.read_text(encoding='utf-8'))
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
            assert bindings.setdefault(member, node['mesh']) == node['mesh']
    report = dict(passed=True, nodes=len(seen), hidden_occurrences=hidden,
                  mesh_resources=len(doc['meshes']), materials=len(doc['materials']),
                  resource_triangles=sum(resource_triangles), visible_triangles=visible_triangles,
                  bounds_metres_min=low.tolist(), bounds_metres_max=high.tolist(),
                  checks=['all XML parent links, names and local/world transforms',
                          'all accessor ranges, finite values and bounds',
                          'every triangle normal and winding', 'shared representation bindings',
                          'default visibility', 'visible triangle count and world bounds match verified OBJ'])
    args.glb.with_suffix('.verification.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
