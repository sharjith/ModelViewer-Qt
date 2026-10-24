#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Original Batch 2 border controls and independent references under out/."""
import argparse
import copy
from fractions import Fraction
import json
import math
from pathlib import Path
import struct

from PIL import Image
from make_texture_batch_controls import assembly
from make_texture_border_component_controls import cubes
from make_texture_border_format_controls import convert
from make_texture_control import geometry
from make_texture_mip_batch_controls import mapped_reference
from make_texture_mip_controls import carrier
from texture_fixture_store import TextureFixtureStore


def ring(interior):
    w, h = interior.size
    out = Image.new('RGBA', (w+2, h+2), (255, 255, 255, 64))
    out.paste(interior, (1, 1))
    for y in range(1, h+1):
        out.putpixel((0, y), (255, 0, 255, 64) if y % 2 else (0, 255, 255, 128))
        out.putpixel((w+1, y), (0, 255, 255, 128))
    for x in range(1, w+1):
        out.putpixel((x, 0), (255, 128, 0, 192))
        out.putpixel((x, h+1), (128, 0, 255, 64))
    return out


def area_resize(source, size):
    """Independent exact-rational area integration, straight RGBA components."""
    w, h = size
    out = Image.new('RGBA', size)
    for y in range(h):
        top, bottom = Fraction(y*source.height, h), Fraction((y+1)*source.height, h)
        for x in range(w):
            left, right = Fraction(x*source.width, w), Fraction((x+1)*source.width, w)
            total = [Fraction(0) for _ in range(4)]
            for sy in range(math.floor(top), math.ceil(bottom)):
                for sx in range(math.floor(left), math.ceil(right)):
                    weight = (min(right, sx+1)-max(left, sx))*(min(bottom, sy+1)-max(top, sy))
                    pixel = source.getpixel((sx, sy))
                    for c in range(4): total[c] += pixel[c]*weight
            scale = (right-left)*(bottom-top)
            values = [v/scale for v in total]
            out.putpixel((x, y), tuple((v.numerator*2+v.denominator)//(2*v.denominator) for v in values))
    return out


def reduce_ring(source):
    w, h = source.width-2, source.height-2
    nw, nh = max(1, w//2), max(1, h//2)
    out = Image.new('RGBA', (nw+2, nh+2))
    out.paste(area_resize(source.crop((1, 1, w+1, h+1)), (nw, nh)), (1, 1))
    for old, new in ((0, 0), (w+1, nw+1)):
        out.paste(area_resize(source.crop((old, 1, old+1, h+1)), (1, nh)), (new, 1))
    for old, new in ((0, 0), (h+1, nh+1)):
        out.paste(area_resize(source.crop((1, old, w+1, old+1)), (nw, 1)), (1, new))
    for x, nx in ((0, 0), (w+1, nw+1)):
        for y, ny in ((0, 0), (h+1, nh+1)): out.putpixel((nx, ny), source.getpixel((x, y)))
    return out


def canonical(source, fmt):
    out = Image.new('RGBA', source.size)
    out.putdata([convert(source.getpixel((x, y)), fmt)[1]
                 for y in range(source.height) for x in range(source.width)])
    return out


def display(source, sampled_s, sampled_t, mip):
    w, h = source.width-2, source.height-2
    cropped = source.crop((0 if sampled_s else 1, 0 if sampled_t else 1,
                           w+2 if sampled_s else w+1, h+2 if sampled_t else h+1))
    if not mip:
        return cropped, (w/cropped.width, h/cropped.height, int(sampled_s)/cropped.width, int(sampled_t)/cropped.height)
    out = Image.new('RGBA', (w*(3 if sampled_s else 1), h*(3 if sampled_t else 1)))
    for y in range(out.height):
        sy = min(h+1, max(0, y-h+1)) if sampled_t else y
        for x in range(out.width):
            sx = min(w+1, max(0, x-w+1)) if sampled_s else x
            out.putpixel((x, y), cropped.getpixel((sx, sy)))
    return out, (1/3 if sampled_s else 1, 1/3 if sampled_t else 1,
                 1/3 if sampled_s else 0, 1/3 if sampled_t else 0)


def legacy_reference(template, store, mapping, clamp_s, clamp_t):
    """Independent quad grid at known cube UV breakpoints, not triangle clipping."""
    ref = copy.deepcopy(template)
    positions, normals, _, _ = cubes()
    points, ns, uv, indices = [], [], [], []
    gx = [0, .25, .75, 1] if clamp_s else [0, 1]
    gy = [0, .25, .75, 1] if clamp_t else [0, 1]
    sx, sy, ox, oy = mapping
    for face in range(6):
        start = len(points); a, b, _, d = positions[4*face:4*face+4]
        for y in gy:
            for x in gx:
                points.append(tuple(a[c]+x*(b[c]-a[c])+y*(d[c]-a[c]) for c in range(3)))
                ns.append(normals[4*face])
                s, t = 2*x-.5, 2*y-.5
                if clamp_s: s = min(1, max(0, s))
                if clamp_t: t = min(1, max(0, t))
                uv.append((s*sx+ox, 1-(t*sy+oy)))
        for y in range(len(gy)-1):
            for x in range(len(gx)-1):
                i = start+y*len(gx)+x
                indices += [i, i+1, i+len(gx)+1, i, i+len(gx)+1, i+len(gx)]
    payload = bytearray()
    buffer_index = len(ref['buffers'])
    def accessor(values, fmt, kind, component, bounds=False):
        offset = len(payload)
        for value in values: payload.extend(struct.pack('<'+fmt, *(value if isinstance(value, tuple) else (value,))))
        view = len(ref['bufferViews'])
        ref['bufferViews'].append(dict(buffer=buffer_index, byteOffset=offset, byteLength=len(payload)-offset))
        item = dict(bufferView=view, componentType=component, count=len(values), type=kind)
        if bounds:
            item.update(min=[min(v[c] for v in values) for c in range(3)], max=[max(v[c] for v in values) for c in range(3)])
        ref['accessors'].append(item)
        return len(ref['accessors'])-1
    attributes = dict(POSITION=accessor(points, '3f', 'VEC3', 5126, True),
                      NORMAL=accessor(ns, '3f', 'VEC3', 5126), TEXCOORD_0=accessor(uv, '2f', 'VEC2', 5126))
    primitive = ref['meshes'][0]['primitives'][0]
    primitive['attributes'] = attributes
    primitive['indices'] = accessor(indices, 'I', 'SCALAR', 5125)
    ref['buffers'].append(dict(uri=store.asset(bytes(payload), '.bin'), byteLength=len(payload)))
    return ref


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('combiner_folder', type=Path)
    parser.add_argument('output_folder', type=Path)
    args = parser.parse_args(); store = TextureFixtureStore(args.output_folder)
    prior = json.loads((args.combiner_folder/'authoring.json').read_text())
    if Path(prior['shared_folder']).resolve() != store.root: raise ValueError('Use existing common assets')
    template = json.loads((store.root/prior['comparison_pairs'][0]['reference']).read_text())
    positions, _, source_uv, indices = cubes()
    uv = [(u-.5, v-.5) for u, v in source_uv]
    front = store.asset(geometry(positions, uv, indices, blend=True), '.jt')
    back = store.asset(geometry([tuple(.55*v for v in p) for p in positions], source_uv, indices, diffuse=(1, 0, 0, 1)), '.jt')
    cases = [
        ('authored_constant_s_repeat', 64, 64, False, 5, 2, 'authored', False, 2, 6),
        ('authored_ring_t_mirror', 64, 64, True, 3, 5, 'authored', False, 2, 6),
        ('authored_ring_s_rect', 64, 32, True, 5, 4, 'authored', False, 2, 6),
        ('authored_constant_t_npot', 35, 64, False, 2, 5, 'authored', False, 2, 6),
        ('authored_ring_both_square', 64, 64, True, 5, 5, 'authored', False, 2, 3),
        ('generated_constant_both', 64, 64, False, 5, 5, 'generated', False, 2, 6),
        ('generated_ring_s_rect_npot', 64, 35, True, 5, 3, 'generated', False, 2, 6),
        ('generated_ring_t_rect', 32, 64, True, 4, 5, 'generated', False, 2, 6),
        ('external_ring_both', 64, 64, True, 5, 5, 'generated', True, 2, 6),
        ('external_ring_s_nearest', 8, 5, True, 5, 2, 'base', True, 2, 1),
        ('external_ring_ignored', 16, 16, True, 3, 2, 'base', True, 2, 2),
        ('legacy_constant_linear', 8, 8, False, 1, 1, 'base', False, 2, 2),
        ('legacy_ring_nearest', 8, 8, True, 1, 2, 'base', False, 2, 1),
        ('legacy_external_linear', 8, 8, True, 1, 1, 'base', True, 2, 2),
        ('constant_luma_alpha_s_generated', 64, 35, False, 5, 3, 'generated', False, 4, 6),
    ]
    colors = [(0, 64, 255, 255), (0, 255, 0, 128), (255, 255, 0, 255), (0, 255, 255, 255),
              (255, 0, 255, 128), (255, 128, 0, 255), (255, 255, 255, 255)]
    authored, pairs, visual = [], [], []
    for label, w, h, explicit, ws, wt, mode, external, fmt, minimum in cases:
        interior = Image.new('RGBA', (w, h))
        for y in range(h):
            for x in range(w): interior.putpixel((x, y), (255, 32, 64, 255) if (x//2+y//2)%2 else (32, 64, 255, 128))
        if mode == 'authored': interior = Image.new('RGBA', (w, h), colors[0])
        stored = [ring(interior) if explicit else interior]
        if mode == 'authored':
            mw, mh = w, h
            while mw > 1 or mh > 1:
                mw, mh = max(1, mw//2), max(1, mh//2)
                im = Image.new('RGBA', (mw, mh), colors[len(stored)%len(colors)])
                stored.append(ring(im) if explicit else im)
        stage = dict(image=stored[0], format=fmt, mode=2, border_mode=2 if explicit else 1,
                     image_border=int(explicit), wrap_s=ws, wrap_t=wt,
                     mag=1 if minimum in (1, 3) else 2, min=minimum, border_color=(1, 0, 1, .25))
        if mode == 'authored': stage['mips'] = stored[1:]
        if external: stage['external'] = store.image(stored[0])
        canonical_levels = []
        for im in stored:
            im = canonical(im, fmt)
            if not explicit:
                border = canonical(Image.new('RGBA', (1, 1), (255, 0, 255, 64)), fmt).getpixel((0, 0))
                full = Image.new('RGBA', (im.width+2, im.height+2), border);full.paste(im, (1, 1));im = full
            canonical_levels.append(im)
        if mode == 'generated':
            while canonical_levels[-1].width > 3 or canonical_levels[-1].height > 3:
                canonical_levels.append(reduce_ring(canonical_levels[-1]))
        s, t = ws in (1, 5), wt in (1, 5)
        levels = [display(im, s, t, mode != 'base')[0] for im in canonical_levels]
        _, mapping = display(canonical_levels[0], s, t, mode != 'base')
        sx, sy, ox, oy = mapping
        if ws == 1 or wt == 1: ref = legacy_reference(template, store, mapping, ws == 1, wt == 1)
        else: ref = mapped_reference(template, store, [(u*sx+ox, 1-(v*sy+oy)) for u, v in uv])
        terminal = ('border-3x3' if s and t else 'border-3x1' if s else 'border-1x3') if mode != 'base' else None
        ref['images'][0]['uri'] = carrier(store, levels[0], levels[1:], terminal) if len(levels)>1 else store.image(levels[0])
        mat = ref['materials'][0];mat['pbrMetallicRoughness']['baseColorFactor'] = [1, 1, 1, 1]
        mat['alphaMode'] = 'BLEND' if any(im.getchannel('A').getextrema()[0]<255 for im in levels) else 'OPAQUE'
        for material in ref['materials']:
            material['doubleSided'] = True;material['pbrMetallicRoughness']['roughnessFactor'] = (2/18)**.5
        wraps = {1:33071, 2:10497, 3:33648, 4:33071, 5:33071}
        ref['samplers'][0].update(wrapS=wraps[ws], wrapT=wraps[wt], magFilter=9728 if stage['mag']==1 else 9729,
                                  minFilter={1:9728, 2:9729, 3:9984, 6:9987}[minimum])
        hashed = store.reference(ref, label);named = 'reference_border_batch_'+label+'.gltf'
        data = (store.root/hashed).read_bytes();target = store.root/named
        if target.exists() and target.read_bytes()!=data: raise ValueError('Reference alias collision')
        target.write_bytes(data);first = None
        for family, minor, version in [(10, 0, 1), (9, 0, 1), (9, 5, 3)]:
            for big in (False, True):
                item = store.control(f'{label}_{family}_{minor}_'+('be' if big else 'le')+'.jt',
                                     assembly(big, family, minor, version, [stage], [20], [], front, back))
                authored.append(item)
                if family==10 and not big: first = item['file']
        visual += [first, named]
        pairs.append(dict(case=label, source=first, reference=named, levels=[list(im.size) for im in levels], terminal=terminal))
    visual += ['texture-border-batch_authored_constant_t_npot_9_0_be.jt',
               'texture-border-batch_external_ring_both_9_5_be.jt',
               'texture-vertex-replace_rgba_replace_10_0_le.jt', 'reference_vertex_rgba_replace.gltf']
    if not all((store.root/name).is_file() for name in visual): raise ValueError('Missing positive source/reference')
    (args.output_folder.parent/'c2ad-visual-files.json').write_text(json.dumps([str(store.root/name) for name in visual], indent=2)+'\n')
    export_folder = args.output_folder.resolve()/'exports';export_folder.mkdir(exist_ok=True)
    exports = [dict(source=p['source'], reference=p['reference'], glb=str(export_folder/(p['case']+'.glb')),
                    gltf=str(export_folder/(p['case']+'.gltf')))
               for p in pairs if p['case'] in ('generated_ring_s_rect_npot', 'external_ring_both', 'legacy_constant_linear')]
    store.report(dict(controls=authored, comparison_pairs=pairs, visual_files=visual, export_pairs=exports,
                      method='Original border images; exact-rational area oracle and independent cube grid clamp reference; no importer execution'))
    print(f'Authored {len(authored)} positive roots; {len(pairs)} comparison pairs; {len(visual)} visual documents')


if __name__ == '__main__': main()
