# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Author local synthetic JT texture controls from one simple local glTF primitive.

This is a bounded fixture writer, not a general glTF/JT converter or a vendor
conformance oracle. Generated/source-derived files must stay under ignored out/.
Requires Pillow for original image decoding; application dependencies are unchanged.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import shutil
import struct
import uuid
import zlib
from PIL import Image


class Bytes:
    def __init__(self, big=False):
        self.big, self.data = big, bytearray()

    def put(self, fmt, *v):
        self.data += struct.pack(('>' if self.big else '<') + fmt, *v)

    def guid(self, value):
        self.data += uuid.UUID(value).bytes if self.big else uuid.UUID(value).bytes_le

    def text(self, value):
        raw = value.encode('utf-16-be' if self.big else 'utf-16-le')
        self.put('I', len(raw) // 2)
        self.data += raw

    def ids(self, values):
        self.put('I', len(values))
        for v in values:
            self.put('I', v)


def kind(prefix):
    return prefix + '-2ac8-11d1-9b6b-0080c7bb5997'


def element(dst, guid, ident, body, base=0):
    dst.put('I', 21 + len(body.data))
    dst.guid(guid)
    dst.put('BI', base, ident)
    dst.data += body.data


def end(dst):
    dst.put('I', 16)
    dst.guid('ffffffff-ffff-ffff-ffff-ffffffffffff')


def packet(dst, values, predictor=None):
    dst.put('BI', 0, len(values))
    for i, value in enumerate(values):
        prediction = 0
        if i >= 4:
            if predictor == 'lag':
                prediction = values[i-1]
            elif predictor == 'stride':
                prediction = 2*values[i-1] - values[i-2]
            elif predictor == 'strip':
                delta = values[i-2] - values[i-4]
                prediction = values[i-2] + (delta if -8 < delta < 8 else 2)
        dst.put('i', value - prediction)


def quantizer(dst, values):
    lo, hi, levels = min(values), max(values), 65536
    step = (hi-lo)/(levels-2)
    bounds = struct.unpack('<ff', struct.pack('<ff', lo-step*0.5, lo-step*0.5+levels*step))
    dst.put('ffB', *bounds, 16)
    actual_step = (bounds[1]-bounds[0])/levels
    codes = [round((v-bounds[0])/actual_step+0.5) if actual_step else 0 for v in values]
    decoded = [bounds[0]+(code-0.5)*actual_step for code in codes]
    assert all(0 <= c <= levels for c in codes)
    assert all(abs(a-b) <= max(abs(actual_step)*0.51, 1e-7) for a, b in zip(values, decoded))
    return codes


def geometry(positions, uvs, indices, store_uv=True):
    big = False
    graph_id = '12345678-1234-5678-9abc-def012345678'
    shape_id = '23456789-1234-5678-9abc-def012345678'
    graph, root, shape, mat = (Bytes(big) for _ in range(4))
    root.put('I', 0); root.ids([20]); root.ids([2]); root.put('I', 0); root.text('')
    root.put('7f6I', *([0]*13)); element(graph, kind('10dd103e'), 1, root)
    shape.put('I', 0); shape.ids([]); shape.put('12ff6IIf', *([0]*12), 1, *([0]*8))
    shape.put('III4B', 0, int(store_uv), 0, 0, 0, 0, 0); element(graph, kind('10dd1077'), 2, shape)
    mat.put('BIH', 8, 0, 0x3980)
    for field in range(4):
        mat.put('4f', *([1]*4 if field == 1 else [0, 0, 0, 1]))
    mat.put('f', 16); element(graph, kind('10dd1030'), 20, mat, 3); end(graph)
    for ident, text in [(100, 'JT_PROP_NAME'), (101, 'Synthetic texture geometry'), (102, 'Textured surface'), (103, 'JT_PROP_LATE_LOADED')]:
        body = Bytes(); body.put('I', 0x40); body.text(text); element(graph, kind('10dd106e'), ident, body)
    late = Bytes(); late.put('I', 0); late.guid(shape_id); late.put('I', 7)
    element(graph, 'e0b05be5-fbbd-11d1-a3a7-00aa00d10954', 104, late); end(graph)
    graph.put('HI', 1, 2); graph.put('4I', 1, 100, 101, 0); graph.put('6I', 2, 100, 102, 103, 104, 0)
    lod = Bytes(); lod.put('H4B4BHH3B4B', 1, 0, int(store_uv), 0, 0, 16, 0, 0, 0, 1, 1, 0, int(store_uv), 0, 16, 0, 0, 0)
    packet(lod, list(range(0, len(indices)+1, 3)), 'stride')
    codes = [quantizer(lod, [p[axis] for p in positions]) for axis in range(3)]
    lod.put('I', len(positions))
    for c in codes:
        packet(lod, c, 'lag')
    if store_uv:
        uv_codes = [quantizer(lod, [uv[axis] for uv in uvs]) for axis in range(2)]
        lod.put('B', 16)
        for c in uv_codes:
            packet(lod, c, 'lag')
    packet(lod, indices, 'strip')
    shape_data = Bytes(); shape_data.put('I', 17+len(lod.data)); shape_data.guid(kind('10dd10ab')); shape_data.put('B', 0); shape_data.data += lod.data
    segment = Bytes(); segment.guid(shape_id); segment.put('II', 7, 24+len(shape_data.data)); segment.data += shape_data.data
    compressed = zlib.compress(graph.data, 1)
    graph_seg = Bytes(); graph_seg.guid(graph_id); graph_seg.put('IIIIB', 1, 33+len(compressed), 2, 1+len(compressed), 2); graph_seg.data += compressed
    file = Bytes(); file.data += b'Version 8.2 JT synthetic source-derived texture geometry'.ljust(75)+b' \n\r\n '
    file.put('BII', 0, 0, 105); file.guid(graph_id); file.put('I', 2)
    file.guid(graph_id); file.put('III', 165+len(segment.data), len(graph_seg.data), 1 << 24)
    file.guid(shape_id); file.put('III', 165, len(segment.data), 7 << 24)
    file.data += segment.data+graph_seg.data
    return file.data


def signed_pixel_component(value,bits,big):
    """Original positive colour control encoding, plus independent expected byte.

    Use signed minimum for a black source pixel. Other values use the closest
    positive level; the GL2 signed-byte profile can express odd positive bytes.
    """
    maximum=(1<<bits)-1
    word=(1<<(bits-1)) if value==0 else (value*(maximum//255))//2
    expected=(value|1) if bits==8 and 0<value<255 else value
    return word.to_bytes(bits//8,'big' if big else 'little'),expected


def assembly(big, inline, rgb, width, height, sampler, transform=None, generation=None, planes=None, pixel_format=1, alignment=1, pixel_type=3, geometry_reference='geometry.jt', image_reference='source_image.png',border_mode=0,image_border=0,border_color=(0,0,0,1)):
    graph, root, part, texture = (Bytes(big) for _ in range(4))
    for body, attributes, children, ref in [(root, [20], [2], ''), (part, [], [], geometry_reference)]:
        body.put('BI', 1, 0); body.ids(attributes); body.put('B', 1); body.ids(children)
        body.put('BI', 1, 0); body.text(ref); body.put('f6I', 0, *([0]*6))
    element(graph, kind('10dd103e'), 1, root); element(graph, kind('10dd103e'), 2, part)
    texture.put('BBIIBI', 1, 8, 0, 0, 1, 9)
    wrap = {33071: 4, 10497: 2, 33648: 3, 33069: 5}
    mag = {9728: 1, 9729: 2}; minimum = {9728: 1, 9729: 2, 9984: 3, 9985: 4, 9986: 5, 9987: 6}
    texture.put('8I', border_mode, mag[sampler.get('magFilter', 9729)], minimum[sampler.get('minFilter', 9987)], wrap[sampler.get('wrapS', 10497)], wrap[sampler.get('wrapT', 10497)], 2, 2, 0)
    texture.put('8f', 0, 0, 0, 1, *border_color)
    texture.put('16f', *(transform if transform is not None else [float(i % 5 == 0) for i in range(16)]))
    texture.put('4I', *(generation if generation is not None else [0]*4))
    texture.put('16f', *(planes if planes is not None else [0]*16))
    texture.put('IIIBI', 0, 0, 0, int(inline), 1)
    if inline:
        texture.put('II6HIHII', pixel_format, pixel_type, 2, alignment, width, height, 1, image_border, 0, 1, len(rgb), len(rgb)); texture.data += rgb
    else:
        texture.text(image_reference)
    element(graph, kind('10dd1073'), 20, texture, 3); end(graph); end(graph); graph.put('HI', 1, 0)
    guid = '12345678-1234-5678-9abc-def012345678'
    seg = Bytes(big); seg.guid(guid); seg.put('IIIIB', 1, 33+len(graph.data), 0, len(graph.data)+1, 0); seg.data += graph.data
    file = Bytes(big); file.data += b'Version 10.0 JT synthetic texture descriptor control'.ljust(75)+b' \n\r\n '
    file.put('BIQ', int(big), 0, 109+len(seg.data)); file.guid(guid); file.data += seg.data
    file.put('I', 1); file.guid(guid); file.put('QII', 109, len(seg.data), 1 << 24)
    return file.data


def main():
    cli = argparse.ArgumentParser(description=__doc__); cli.add_argument('source', type=Path); cli.add_argument('output', type=Path); args = cli.parse_args()
    source, output = args.source.resolve(), args.output.resolve()
    repo = Path(__file__).resolve().parents[2]
    if not output.is_relative_to(repo/'out'):
        raise ValueError('Fixture output must be inside ignored repository out/')
    doc = json.loads(source.read_text(encoding='utf-8'))
    if len(doc['meshes']) != 1 or len(doc['meshes'][0]['primitives']) != 1 or doc.get('extensionsRequired'):
        raise ValueError('Expected one simple primitive without required extensions')
    if len(doc['images']) != 1 or sum('mesh' in node for node in doc['nodes']) != 1:
        raise ValueError('This writer requires one image and one placed mesh')
    if any(any(k in node for k in ['matrix', 'translation', 'rotation', 'scale', 'skin']) for node in doc['nodes']):
        raise ValueError('This control writer requires identity node transforms')
    primitive = doc['meshes'][0]['primitives'][0]
    if primitive.get('mode', 4) != 4:
        raise ValueError('Triangle lists only')
    blobs = [(source.parent/buffer['uri']).read_bytes() for buffer in doc['buffers']]
    def accessor(index):
        a = doc['accessors'][index]; view = doc['bufferViews'][a['bufferView']]
        if a.get('sparse') or a.get('normalized'):
            raise ValueError('Sparse/normalized accessors unsupported by this writer')
        component = {5121: 'B', 5123: 'H', 5125: 'I', 5126: 'f'}[a['componentType']]
        count = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3}[a['type']]; fmt = '<'+component*count
        stride = view.get('byteStride', struct.calcsize(fmt)); offset = view.get('byteOffset', 0)+a.get('byteOffset', 0)
        return [struct.unpack_from(fmt, blobs[view['buffer']], offset+i*stride) for i in range(a['count'])]
    positions = accessor(primitive['attributes']['POSITION']); source_uv = accessor(primitive['attributes']['TEXCOORD_0'])
    uvs = [(u, 1-v) for u, v in source_uv] # glTF top-left to OpenGL/JT bottom-left
    indices = [v[0] for v in accessor(primitive['indices'])]
    assert len(indices)%3 == 0 and len(positions) == len(uvs) and max(indices) < len(positions)
    assert all(math.isfinite(v) for row in positions+uvs for v in row)
    assert len(positions) <= 65536 and len(indices) <= 196608
    material = doc['materials'][primitive['material']]['pbrMetallicRoughness']
    if material.get('baseColorFactor', [1]*4) != [1]*4:
        raise ValueError('This control writer requires a white base-colour factor')
    binding = material['baseColorTexture']; assert binding.get('texCoord', 0) == 0 and not binding.get('extensions')
    tex = doc['textures'][binding['index']]; image_path = source.parent/doc['images'][tex['source']]['uri']
    decoded_image = Image.open(image_path).convert('RGBA')
    if decoded_image.getchannel('A').getextrema() != (255, 255):
        raise ValueError('This initial texture control requires an opaque source image')
    image = decoded_image.convert('RGB'); width, height = image.size
    assert 0 < width < 32768 and 0 < height < 32768 and width*height*3 <= 64*1024*1024
    rgb = image.transpose(Image.Transpose.FLIP_TOP_BOTTOM).tobytes()
    output.mkdir(parents=True, exist_ok=True); image.save(output/'source_image.png')
    (output/'geometry.jt').write_bytes(geometry(positions, uvs, indices))
    sampler = doc.get('samplers', [{}])[tex.get('sampler', 0)]
    for big in [False, True]:
        for inline in [False, True]:
            (output/f'texture_{"inline" if inline else "external"}_{"be" if big else "le"}.jt').write_bytes(assembly(big, inline, rgb, width, height, sampler))
    for i, buffer in enumerate(doc['buffers']):
        buffer['uri'] = f'source_buffer_{i}.bin'; (output/buffer['uri']).write_bytes(blobs[i])
    doc['images'][tex['source']]['uri'] = 'source_image.png'; (output/'reference.gltf').write_text(json.dumps(doc, indent=2)+'\n', encoding='utf-8')
    license_path = source.parent.parent/'LICENSE.md'
    if license_path.exists():
        shutil.copyfile(license_path, output/'SOURCE_LICENSE.md')
    report = {'source': str(source), 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(), 'source_image_sha256': hashlib.sha256(image_path.read_bytes()).hexdigest(), 'vertices': len(positions), 'triangles': len(indices)//3, 'image': [width, height], 'method': 'Synthetic mixed 10.0 descriptor / 8.2 quantized geometry; V and inline row origin converted; not a vendor export or tested JT reader result'}
    (output/'authoring.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({k: report[k] for k in ['vertices', 'triangles', 'image']}))


if __name__ == '__main__':
    main()
