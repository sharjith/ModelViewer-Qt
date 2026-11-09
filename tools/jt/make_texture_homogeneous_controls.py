#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Original exact homogeneous-to-UV mapping controls; authoring under out/ only."""
import argparse
import copy
import json
import struct
from pathlib import Path

from PIL import Image
from make_texture_batch_controls import assembly, IDENTITY
from make_texture_control import geometry
from texture_fixture_store import TextureFixtureStore


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('border_folder', type=Path)
    parser.add_argument('output_folder', type=Path)
    args = parser.parse_args()
    store = TextureFixtureStore(args.output_folder)
    prior = json.loads((args.border_folder/'authoring.json').read_text())
    if Path(prior['shared_folder']).resolve() != store.root:
        raise ValueError('Use the existing common resources')
    # Two disjoint XY panels. Z is constant on each face, but differs between
    # panels, providing an independently known per-face homogeneous denominator.
    positions = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0),
                 (2, 0, 1), (3, 0, 1), (3, 1, 1), (2, 1, 1)]
    uv = [(0, 0), (2, 0), (2, 2), (0, 2)]*2
    normals = [(0, 0, 1)]*8
    indices = [0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7]
    stored = store.asset(geometry(positions, uv, indices, blend=True), '.jt')
    generated = store.asset(geometry(positions, uv, indices, store_uv=False, blend=True), '.jt')
    image = Image.new('RGBA', (16, 16))
    palette = [(255, 48, 32, 255), (32, 224, 64, 255), (32, 64, 255, 128), (255, 224, 32, 255)]
    for y in range(16):
        for x in range(16): image.putpixel((x, y), palette[((x//4)+(y//4)) % 4])
    image_uri = store.image(image)
    vertices = bytearray()
    for p in positions: vertices.extend(struct.pack('<3f', *p))
    for n in normals: vertices.extend(struct.pack('<3f', *n))
    for i in indices: vertices.extend(struct.pack('<H', i))
    template = dict(asset=dict(version='2.0', generator='ModelViewer original homogeneous mapping reference'),
        buffers=[dict(uri=store.asset(bytes(vertices), '.bin'), byteLength=len(vertices))],
        bufferViews=[dict(buffer=0, byteOffset=0, byteLength=96),
                     dict(buffer=0, byteOffset=96, byteLength=96),
                     dict(buffer=0, byteOffset=192, byteLength=24)],
        accessors=[dict(bufferView=0, componentType=5126, count=8, type='VEC3', min=[0, 0, 0], max=[3, 1, 1]),
                   dict(bufferView=1, componentType=5126, count=8, type='VEC3'),
                   dict(bufferView=2, componentType=5123, count=12, type='SCALAR')],
        images=[dict(uri=image_uri)], textures=[dict(source=0, sampler=0)],
        samplers=[dict(wrapS=10497, wrapT=10497, magFilter=9729, minFilter=9729)],
        materials=[dict(doubleSided=True, alphaMode='BLEND', pbrMetallicRoughness=dict(
            baseColorFactor=[1, 1, 1, 1], metallicFactor=0, roughnessFactor=(2/18)**.5, baseColorTexture=dict(index=0)))],
        meshes=[dict(primitives=[dict(attributes=dict(POSITION=0, NORMAL=1), indices=2, material=0)])],
        nodes=[dict(mesh=0, name='Two original parallel panels')], scenes=[dict(nodes=[0])], scene=0)

    def recipe(q=1, r=None, q_plane=None, st=False):
        matrix = IDENTITY.copy(); matrix[15] = q
        modes, planes = [0]*4, [0.0]*16
        if r is not None: modes[2] = 1; planes[8+int(r)] = 1
        if q_plane is not None: modes[3] = 1; planes[14] = 1; planes[15] = q_plane; matrix[15] = 1
        if st: modes[:2] = [1, 1]; planes[0] = planes[5] = 1
        return dict(image=image, format=2, mode=2, mag=2, min=2, wrap_s=2, wrap_t=2,
                    transform=matrix, generation=modes, planes=planes)

    # Expected UV expressions are stated independently, without evaluating the
    # source's transform/generation descriptor to obtain an expected result.
    cases = []
    cases.append(('constant_q_half', recipe(q=2), False, lambda u, v, x, y, z: (u/2, v/2)))
    cases.append(('negative_q', recipe(q=-2), False, lambda u, v, x, y, z: (-u/2, -v/2)))
    spec = recipe(q=2); spec['transform'][0] = 2; spec['transform'][4] = .5; spec['transform'][12] = .5
    spec['transform'][2] = 7; spec['transform'][14] = 3  # ignored 2D R output
    cases.append(('homogeneous_shear', spec, False, lambda u, v, x, y, z: (u+v/4+.25, v/2)))
    spec = recipe(q=2, r=0); spec['transform'][8] = .5; spec['transform'][9] = .25
    cases.append(('generated_r_to_st', spec, False, lambda u, v, x, y, z: ((u+x/2)/2, (v+x/4)/2)))
    cases.append(('generated_q_two_planes', recipe(q_plane=2), False, lambda u, v, x, y, z: (u/(z+2), v/(z+2))))
    cases.append(('generated_stq_no_uv', recipe(q_plane=2, st=True), True, lambda u, v, x, y, z: (x/(z+2), y/(z+2))))
    spec = recipe(q=2, r=2); spec['transform'][11] = 1
    cases.append(('generated_r_to_q', spec, False, lambda u, v, x, y, z: (u/(z+2), v/(z+2))))
    spec = recipe(q_plane=2, r=2); spec['transform'][11] = .5
    cases.append(('generated_rq_mix', spec, False, lambda u, v, x, y, z: (u/(1.5*z+2), v/(1.5*z+2))))
    cases.append(('negative_generated_q', recipe(q_plane=-2), False, lambda u, v, x, y, z: (u/(z-2), v/(z-2))))
    spec = recipe(q=2); spec.update(border_mode=1, wrap_s=5, wrap_t=3, border_color=(1, 0, 1, .25))
    cases.append(('constant_q_border_s', spec, False, lambda u, v, x, y, z: ((u*8+1)/18, v/2)))
    authored, pairs, visual = [], [], []
    for label, spec, no_uv, expected in cases:
        reference = copy.deepcopy(template)
        mapped = [expected(u, v, *p) for (u, v), p in zip(uv, positions)]
        payload = b''.join(struct.pack('<ff', s, 1-t) for s, t in mapped)
        reference['buffers'].append(dict(uri=store.asset(payload, '.bin'), byteLength=len(payload)))
        reference['bufferViews'].append(dict(buffer=1, byteLength=len(payload)))
        reference['accessors'].append(dict(bufferView=3, componentType=5126, count=8, type='VEC2'))
        reference['meshes'][0]['primitives'][0]['attributes']['TEXCOORD_0'] = 3
        if label == 'constant_q_border_s':
            padded = Image.new('RGBA', (18, 16), (255, 0, 255, 64)); padded.paste(image, (1, 0))
            reference['images'][0]['uri'] = store.image(padded)
            reference['samplers'][0].update(wrapS=33071, wrapT=33648)
        store.reference(reference, label)
        reference_name = 'reference_homogeneous_'+label+'.gltf'
        (store.root/reference_name).write_text(json.dumps(reference, sort_keys=True, indent=2)+'\n')
        first = None
        for family, minor, version in [(10, 0, 1), (9, 0, 1), (9, 5, 3)]:
            for big in (False, True):
                item = store.control(f'{label}_{family}_{minor}_'+('be' if big else 'le')+'.jt',
                    assembly(big, family, minor, version, [spec], [20], [], generated if no_uv else stored, ''))
                authored.append(item)
                if family == 10 and not big: first = item['file']
        visual += [first, reference_name]
        pairs.append(dict(case=label, source=first, reference=reference_name, expected_uv=mapped,
            source_program={key:spec[key] for key in ('transform', 'generation', 'planes')}))
    negative_specs = []
    spec = recipe(q=0); negative_specs.append(('zero_q', spec, False))
    spec = recipe(); spec['transform'][3] = .25; negative_specs.append(('varying_q', spec, False))
    spec = recipe(); spec['transform'][3] = 1e-7; negative_specs.append(('near_varying_q', spec, False))
    for mode, name in [(2, 'view_linear'), (3, 'sphere'), (4, 'reflection'), (5, 'normal')]:
        spec = recipe(); spec['generation'][0] = mode; negative_specs.append((name, spec, False))
    spec = recipe(q_plane=2); spec['planes'][14] = 2; negative_specs.append(('nonunit_q_plane', spec, False))
    spec = recipe(); spec['generation'][0] = 1; spec['planes'][0] = 1; negative_specs.append(('missing_partial_uv', spec, True))
    negative = []
    for label, spec, no_uv in negative_specs:
        negative.append(store.control('unsupported_'+label+'_10_0_le.jt',
            assembly(False, 10, 0, 1, [spec], [20], [], generated if no_uv else stored, '')))
    visual += ['texture-homogeneous_generated_q_two_planes_9_0_be.jt',
               'texture-homogeneous_generated_rq_mix_9_5_be.jt',
               'texture-border-batch_generated_ring_s_rect_npot_10_0_le.jt',
               'reference_border_batch_generated_ring_s_rect_npot.gltf',
               'texture-border-batch_legacy_constant_linear_10_0_le.jt',
               'reference_border_batch_legacy_constant_linear.gltf']
    if not all((store.root/p).is_file() for p in visual): raise ValueError('Missing original positive or accepted regression')
    launch = args.output_folder.parent/'c2ae-visual-files.json'
    launch.write_text(json.dumps([str(store.root/p) for p in visual], indent=2)+'\n')
    exports = [dict(source=p['source'], reference=p['reference'],
        glb=str(store.gate/'exports'/(p['case']+'.glb')), gltf=str(store.gate/'exports'/(p['case']+'.gltf')))
        for p in pairs if p['case'] in ('generated_stq_no_uv', 'generated_r_to_st')]
    (store.gate/'exports').mkdir(exist_ok=True)
    store.report(dict(controls=authored, unsupported=negative, comparison_pairs=pairs, positions=positions, source_uv=uv,
        launch_file=str(launch), export_pairs=exports,
        method='Original parallel-panel geometry and images; independently stated exact UV expressions; no importer/native execution or authentic-export qualification'))
    print(f'Authored {len(authored)} positive roots, {len(negative)} unsupported roots; {len(pairs)} pairs / {len(visual)} launch documents')


if __name__ == '__main__': main()
