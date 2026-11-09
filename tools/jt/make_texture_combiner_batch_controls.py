#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Original constant-vertex combiner and mixed-resolution controls under out/."""
import argparse
import hashlib
import json
import math
from pathlib import Path

from PIL import Image
from make_texture_batch_controls import assembly, composed
from make_texture_border_component_controls import cubes
from make_texture_control import geometry
from make_texture_mip_batch_controls import mapped_reference
from texture_fixture_store import TextureFixtureStore


VERTEX = (.25, .5, .75, .25)
MATERIAL = (.2, .7, .4, .5)


def expected_image(stages, incoming):
    """Resample independent nearest texel partitions before the existing oracle.

    Pillow's nearest resize duplicates texels on an integer common grid; it does
    not read the importer result. No filtered/nonlinear approximation is used.
    """
    width = math.lcm(*(s['image'].width for s in stages))
    height = math.lcm(*(s['image'].height for s in stages))
    expanded = [dict(s, image=s['image'].resize((width, height), Image.Resampling.NEAREST)) for s in stages]
    return composed(expanded, incoming)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('vertex_replace_folder', type=Path)
    parser.add_argument('output_folder', type=Path)
    args = parser.parse_args()
    store = TextureFixtureStore(args.output_folder)
    prior = json.loads((args.vertex_replace_folder / 'authoring.json').read_text())
    if Path(prior['shared_folder']).resolve() != store.root:
        raise ValueError('Use the existing shared store')
    template = json.loads((store.root / prior['descriptive_references']['rgba_replace']).read_text())
    positions, _, source_uv, indices = cubes()
    uv = [(u*.5, v*.5) for u, v in source_uv]
    front = store.asset(geometry(positions, uv, indices, blend=True, diffuse=MATERIAL, vertex_color=VERTEX), '.jt')
    plain = store.asset(geometry(positions, uv, indices, blend=True, diffuse=MATERIAL), '.jt')
    back = store.asset(geometry([tuple(.55*v for v in p) for p in positions], source_uv, indices, diffuse=(1, 0, 0, 1)), '.jt')
    checker = Image.new('RGBA', (8, 8))
    for y in range(8):
        for x in range(8):
            checker.putpixel((x, y), (255, 32, 64, 255) if (x//2+y//2) % 2 else (32, 64, 255, 96))
    coarse = Image.new('RGBA', (2, 3))
    coarse.putdata([(255, 64, 32, 255), (32, 255, 64, 128), (64, 32, 255, 192),
                    (255, 255, 255, 255), (255, 128, 0, 64), (32, 192, 255, 255)])
    fine = Image.new('RGBA', (3, 2))
    fine.putdata([(64, 255, 255, 255), (255, 64, 255, 128), (255, 255, 64, 192),
                  (255, 255, 255, 255), (128, 64, 32, 255), (32, 128, 255, 128)])
    def stage(mode, image=checker, unit=0, fmt=2, linear=False, **extra):
        return dict(image=image, mode=mode, unit=unit, format=fmt,
                    mag=2 if linear else 1, min=2 if linear else 1, **extra)
    blend = (.9, .1, .2, 1)
    cases = [
        ('vertex_decal_rgb_linear', [stage(1, fmt=1, linear=True)], True),
        ('vertex_decal_rgba_nearest', [stage(1)], True),
        ('vertex_blend_rgba_linear', [stage(4, linear=True, blend=blend)], True),
        ('vertex_add_rgba_nearest', [stage(5)], True),
        ('vertex_replace_luminance_linear', [stage(3, fmt=3, linear=True)], True),
        ('vertex_replace_luminance_alpha_linear', [stage(3, fmt=4, linear=True)], True),
        ('vertex_replace_alpha_linear', [stage(3, fmt=10, linear=True)], True),
        ('vertex_replace_external_rgba', [stage(3, linear=True, external=store.image(checker))], True),
        ('vertex_stack_modulate_add', [stage(2), stage(5, checker.transpose(Image.Transpose.FLIP_LEFT_RIGHT), unit=3)], True),
        ('vertex_stack_modulate_replace_alpha', [stage(2, fmt=1), stage(3, checker, unit=3, fmt=10)], True),
        ('vertex_multires_modulate', [stage(2, coarse), stage(2, fine, unit=3)], True),
        ('material_multires_decal', [stage(3, coarse), stage(1, fine, unit=3)], False),
        ('vertex_inherited_multires', [stage(2, coarse), stage(5, fine, unit=3), stage(3, coarse)], True),
    ]
    authored, visual, pairs = [], [], []
    for label, stages, vertex in cases:
        incoming = VERTEX if vertex else MATERIAL
        parent, child = list(range(20, 20+len(stages))), []
        effective = stages
        if label == 'vertex_inherited_multires':
            parent, child = [21, 20], [22]
            effective = [stages[2], stages[1]]
        reference = mapped_reference(template, store, [(u, 1-v) for u, v in uv])
        mat = reference['materials'][0]
        mat['pbrMetallicRoughness']['baseColorFactor'] = [1, 1, 1, 1]
        # Preserve incoming alpha as a factor, rather than rounding it into PNG.
        # Products/replacements also keep the surviving RGB dependence separate.
        if vertex:
            factorable = all(s['mode'] in (2, 3) for s in effective)
            pixels = expected_image(effective, (1, 1, 1, 1) if factorable else (*incoming[:3], 1))
            factors = list(incoming) if factorable else [1, 1, 1, incoming[3]]
            for s in effective:
                if s['mode'] == 3:
                    if s['format'] != 10: factors[:3] = [1, 1, 1]
                    if s['format'] in (2, 4, 10, 12): factors[3] = 1
            mat['pbrMetallicRoughness']['baseColorFactor'] = factors
        else:
            pixels = expected_image(effective, incoming)
        reference['images'][0]['uri'] = store.image(pixels)
        mat['alphaMode'] = 'BLEND'
        for material in reference['materials']:
            material['doubleSided'] = True
            material['pbrMetallicRoughness']['roughnessFactor'] = (2/18)**.5
        reference['samplers'][0].update(wrapS=33071, wrapT=33071,
                                        magFilter=9729 if stages[0]['mag'] == 2 else 9728,
                                        minFilter=9729 if stages[0]['min'] == 2 else 9728)
        hashed = store.reference(reference, label)
        named = 'reference_combiner_batch_' + label + '.gltf'
        data = (store.root / hashed).read_bytes()
        target = store.root / named
        if target.exists() and target.read_bytes() != data:
            raise ValueError('Reference alias collision')
        target.write_bytes(data)
        first = None
        for family, minor, version in [(10, 0, 1), (9, 0, 1), (9, 5, 3)]:
            for big in (False, True):
                item = store.control(f'{label}_{family}_{minor}_' + ('be' if big else 'le') + '.jt',
                                     assembly(big, family, minor, version, stages, parent, child, front if vertex else plain, back))
                authored.append(item)
                if family == 10 and not big:
                    first = item['file']
        visual += [first, named]
        pairs.append(dict(case=label, source=first, reference=named, expected_size=list(pixels.size),
                          image_sha256=hashlib.sha256(pixels.tobytes()).hexdigest()))
    visual += ['texture-combiner-batch_vertex_decal_rgba_nearest_9_0_be.jt',
               'texture-combiner-batch_vertex_multires_modulate_9_5_be.jt',
               'texture-vertex-replace_rgba_replace_10_0_le.jt', 'reference_vertex_rgba_replace.gltf',
               'texture-affine-filter_blend_linear_10_0_le.jt']
    if not all((store.root / p).is_file() for p in visual):
        raise ValueError('Missing original positive control or shared regression')
    launch = args.output_folder.parent / 'c2ac-visual-files.json'
    launch.write_text(json.dumps([str(store.root / p) for p in visual], indent=2) + '\n')
    export_folder = args.output_folder.resolve() / 'exports'
    export_folder.mkdir(exist_ok=True)
    exports = [dict(source=p['source'], reference=p['reference'],
                    glb=str(export_folder / (p['case'] + '.glb')),
                    gltf=str(export_folder / (p['case'] + '.gltf')))
               for p in pairs if p['case'] in ('vertex_replace_external_rgba',
                                               'vertex_stack_modulate_replace_alpha',
                                               'vertex_inherited_multires')]
    store.report(dict(controls=authored, visual_files=visual, comparison_pairs=pairs,
                      export_pairs=exports,
                      method='Original constant RGBA geometry and literal source images; independent nearest-grid/combiner oracle; no importer execution'))
    print(f'Authored {len(authored)} positive roots; {len(pairs)} comparison pairs; {len(visual)} visual documents')


if __name__ == '__main__':
    main()
