"""Focused source-byte normal audit; no external decoder or application changes.

Separates pool quantizer uncertainty from normal/winding disagreements that
cannot be explained by that uncertainty. Analytic normals are geometric
references, not access to the exporter's original normal floats.
"""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import runpy
import struct
import xml.etree.ElementTree as ET
import zipfile

import numpy as np


FIXTURES = {'ISO_2338_5x25_Zyl_Stift.3DRep', 'Distanzring_1_R.3DRep',
            'Kugel_3_4.3DRep', 'Kugel_3_4_50.3DRep'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    api = runpy.run_path(str(Path(__file__).with_name('decode_representation.py')))
    totals, tags, opposed_kinds = Counter(), Counter(), Counter()
    members, fixtures, examples = [], [], []
    with zipfile.ZipFile(args.archive) as archive:
        local = lambda e: e.tag.rsplit('}', 1)[-1]
        manifest = ET.fromstring(archive.read('Manifest.xml'))
        root_member = next(e.text.strip() for e in manifest.iter() if local(e) == 'Root')
        product = ET.fromstring(archive.read(root_member))
        formats = {e.attrib['associatedFile'].removeprefix('urn:3DXML:'): e.attrib['format']
                   for e in product.iter() if local(e) == 'ReferenceRep'}
        for name in archive.namelist():
            if not name.endswith('.3DRep'):
                continue
            data = archive.read(name)
            # Route the review attachment by XML metadata, including UTF-16 XML.
            if formats[name] != 'UVR':
                continue
            entries = api['compound_streams'](data)
            skeleton = next(e for e in entries if e['name'] == 'Skeleton')
            member = Counter()
            def visit(leaf, mesh, groups, styles):
                points = np.asarray(mesh['positions_serialized'], dtype=np.float64)
                normals = np.asarray(api['MESH']['normal_candidates'](mesh))
                normal_tags = np.asarray(mesh['normal_tags'])
                tags.update(mesh['normal_tags'])
                member['leaves'] += 1
                member['normal_records'] += len(normals)
                member['axis_records'] += int(np.sum(normal_tags >= 6))
                assert np.all(np.isfinite(normals))
                assert np.max(np.abs(np.linalg.norm(normals, axis=1)-1)) < 1e-12
                for fi, face in enumerate(mesh['finest_faces']):
                    if not face['triangles']:
                        continue
                    indices = np.asarray(face['triangles'], dtype=np.int64)
                    triangle = points[indices]
                    cross = np.cross(triangle[:, 1]-triangle[:, 0], triangle[:, 2]-triangle[:, 0])
                    lengths = np.linalg.norm(cross, axis=1)
                    assert np.all(lengths > 0)
                    unit = cross / lengths[:, None]
                    constant = face['constant_normal_serialized']
                    if constant is not None:
                        member['constant_normal_triangles'] += len(indices)
                        member['opposed_constant_normal_triangles'] += int(np.sum(unit @ constant <= 0))
                        continue
                    assert indices.max() < len(normals)
                    dots = np.einsum('ij,ikj->ik', unit, normals[indices])
                    member['pool_normal_corners'] += dots.size
                    for ti, ci in zip(*np.where(dots <= 0)):
                        vertex = int(indices[ti, ci])
                        cosine = float(dots[ti, ci])
                        member['opposed_pool_corners'] += 1
                        member['opposed_exact_axis_corners'] += int(normal_tags[vertex] >= 6)
                        member['opposed_with_dot_below_minus_0_001'] += int(cosine < -.001)
                        member['opposed_with_dot_below_minus_0_01'] += int(cosine < -.01)
                        k = int(ti)-face['triangle_list_count']
                        kind = 'list'
                        if k >= 0:
                            kind = 'strip'
                            for length in face['strip_lengths']:
                                if k < length-2:
                                    break
                                k -= length-2
                            else:
                                kind = 'fan'
                        opposed_kinds[kind] += 1
                        example = dict(member=name, leaf_offset=leaf['offset'], face=fi,
                            triangle=int(ti), vertex=vertex, tag=int(normal_tags[vertex]),
                            dot=cosine, angle_degrees=math.degrees(math.acos(max(-1, cosine))),
                            primitive_kind=kind, normal=normals[vertex].tolist(),
                            geometric_normal=unit[ti].tolist())
                        examples.append(example)
                if name in FIXTURES:
                    used = {i for f in mesh['finest_faces'] if f['constant_normal_serialized'] is None
                            for i in f['indices']}
                    center = struct.unpack_from('<3f', bytes.fromhex(leaf['header_hex']), 19)
                    coefficients, cursor, angles = [], 0, []
                    for i, tag in enumerate(normal_tags):
                        words = mesh['normal_words_signed_le'][cursor:cursor+2] if tag < 6 else ()
                        cursor += len(words)
                        if i not in used:
                            continue
                        point = points[i]
                        if name.startswith('Kugel'):
                            expected = point-center
                        else:
                            if abs(normals[i, 0]) > 1e-12:
                                continue
                            radius = np.linalg.norm(point[1:])
                            expected = np.array([0, point[1], point[2]])
                            if name.startswith('Distanz') and radius < 30:
                                expected *= -1
                        expected /= np.linalg.norm(expected)
                        angles.append(math.degrees(math.acos(np.clip(expected @ normals[i], -1, 1))))
                        axes = [j for j in range(3) if j != tag//2]
                        coefficients.extend((abs(w), abs(expected[j])) for w, j in zip(words, axes))
                    w, x = np.asarray(coefficients).T
                    models = []
                    for scale in (32760, 32763, 32764, 32765, 32766, 32767, 32768):
                        for rounding in ('truncate', 'nearest'):
                            predicted = np.floor(x*scale) if rounding == 'truncate' else np.rint(x*scale)
                            error = np.abs(predicted-w)
                            models.append(dict(scale=scale, rounding=rounding,
                                coefficient_mismatches=int(np.sum(error != 0)),
                                max_word_error=float(error.max())))
                    residual = x*32767-w
                    fixtures.append(dict(member=name, selected_vertices=len(angles),
                        coefficient_comparisons=len(w), max_direction_error_degrees=max(angles),
                        magnitude_residual_range=[float(residual.min()), float(residual.max())],
                        models=models, exact_model_found=any(m['coefficient_mismatches'] == 0 for m in models)))
            api['skeleton_graph'](api['join_stream'](data, skeleton), visit)
            members.append(dict(member=name, **member))
            totals.update(member)
            print(name, member['opposed_pool_corners'], 'opposed pool corners', flush=True)
    report = dict(status='dominant-axis reconstruction corroborated; exact quantizer unresolved',
        source_sha256=hashlib.sha256(args.archive.read_bytes()).hexdigest(),
        totals=dict(totals), tag_histogram=dict(sorted(tags.items())),
        opposed_corner_primitive_kinds=dict(opposed_kinds), analytic_fixtures=fixtures,
        members_with_opposed_corners=[r for r in members if r.get('opposed_pool_corners', 0)],
        opposed_corner_details=examples,
        conclusions=[
            'No tested integer scale/rounding model matches every analytic fixture coefficient.',
            'Analytic references do not reveal original exporter normal floats or prove a codec.',
            'Exact-axis opposed corners do not involve a coefficient quantizer.',
            'Substantial opposed directions cannot be repaired by the tiny analytic-fixture quantizer corrections.',
            'Source normal binding, tessellation and normal direction need an independent oracle to separate completely.',
            'Keep flat normals as the verified default; retain raw pool words/tags and constant-face overrides.',
            'Do not automatically flip normals, alter winding or substitute fitted coefficient offsets.'])
    args.report.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(dict(totals), indent=2))


if __name__ == '__main__':
    main()
