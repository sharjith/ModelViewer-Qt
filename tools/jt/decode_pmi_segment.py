#!/usr/bin/env python3
"""Spec-based decoder for the PMI Manager Meta Data Element in the PMI data segment (segment type 3) of a JT file.

Follows the JT 10.6 format description, section 8.3 (PMI Manager Meta Data Element, PMI String Table, Generic PMI Entities, PMI
2D Data, Non-Text Polyline Data). Written to check the vendored C++ reader and to study Solid Edge exports; it is a research tool
and not the importer. Supports the LZMA-compressed segment layout of Solid Edge 2026 JT 10.10 exports and the field order the
controls demonstrated; anything it cannot place is reported, never guessed.

Usage: python -B tools/jt/decode_pmi_segment.py INPUT.jt --output out/jt-research/<topic>/pmi-decoded.json
The input is only read. The output holds the decoded model strings and geometry; keep it under out/.
"""
import argparse
import json
import lzma
import struct
import sys

PMI_SEGMENT = 3
NO_STRING = 0xFFFFFFFF
ENTITY_TYPES = {
    0x0001: 'PMI', 0x0002: 'Weld', 0x0004: 'Spot Weld', 0x0008: 'Line Weld', 0x0010: 'Groove Weld', 0x0011: 'Fillet Weld',
    0x0012: 'Slot Weld', 0x0014: 'Edge Weld', 0x0018: 'Arc Spot Weld', 0x0020: 'Resistance Spot Weld',
    0x0021: 'Resistance Seam Weld', 0x0022: 'Structural Adhesive Bead', 0x0024: 'Structural Adhesive Tape',
    0x0028: 'Structural Adhesive Dollop', 0x0040: 'Mechanical Clinch', 0x0041: 'Surface Finish', 0x0042: 'Measurement Point',
    0x0044: 'Datum Locator', 0x0048: 'Certification Point', 0x0080: 'GD&T', 0x0081: 'Feature Control Frame',
    0x0082: 'Dimension', 0x0084: 'Datum Feature Symbol', 0x0088: 'Datum Target', 0x0100: 'Note',
    0x0101: 'Face Attribute Note', 0x0102: 'Model View Label Note', 0x0104: 'Coordinate System',
    0x0108: 'Reference Geometry', 0x0110: 'Reference Point', 0x0111: 'Reference Axis', 0x0112: 'Reference Plane',
    0x0114: 'User Defined', 0x0118: 'Measurement Locator', 0x0120: 'Datum Point', 0x0230: 'Fastener PMI',
    0x0231: 'Material specification', 0x0232: 'Process specification', 0x0233: 'Part specification', 0x0235: 'Balloon Note',
    0x0238: 'Circle Centre', 0x0239: 'Coordinate Note', 0x0240: 'AttributeNote', 0x0241: 'Bundle or Dressing Note',
    0x0242: 'Cutting Plane Symbol', 0x0243: 'Crosshatch', 0x0244: 'E Marking (Note)', 0x0245: 'Organization',
    0x0246: 'Region', 0x0305: 'Section', 0x0306: 'Centreline', 0x0307: 'Fit Designation',
    0x0308: 'Composite Feature Control Frame', 0x0309: 'Weld Note', 0x030A: 'Reference Circle', 0x030B: 'Reference Cylinder',
    0x030C: 'Part Transform', 0x030D: 'Callout Dimension', 0x030F: 'Chamfer Dimension', 0x0310: 'Model View Style',
    0x0311: 'PMI Table', 0x0313: 'Callout Fit Designation',
}


class Reader:
    def __init__(self, data, position=0):
        self.data, self.position = data, position

    def take(self, size):
        if self.position + size > len(self.data):
            raise EOFError('read past the end at %#x (+%d)' % (self.position, size))
        chunk = self.data[self.position:self.position + size]
        self.position += size
        return chunk

    def unpack(self, fmt):
        value = struct.unpack('<' + fmt, self.take(struct.calcsize('<' + fmt)))
        return value[0] if len(value) == 1 else value

    def u8(self): return self.unpack('B')
    def i16(self): return self.unpack('h')
    def u16(self): return self.unpack('H')
    def i32(self): return self.unpack('i')
    def u32(self): return self.unpack('I')
    def f32(self): return self.unpack('f')

    def count(self, limit=10_000_000):
        value = self.i32()
        if value < 0 or value > limit:
            raise ValueError('implausible count %d at %#x' % (value, self.position - 4))
        return value

    def vec(self, fmt):
        count = self.count()
        return list(struct.unpack('<%d%s' % (count, fmt), self.take(count * struct.calcsize(fmt))))

    def mbstring(self):
        length = self.count(1 << 22)
        return self.take(2 * length).decode('utf-16le')


def segment_payload(data):
    offset = struct.unpack_from('<q', data, 85)[0]
    count = struct.unpack_from('<i', data, offset)[0]
    position = offset + 4
    for _ in range(count):
        seg_offset, length, attributes = struct.unpack_from('<qiI', data, position + 16)
        position += 32
        if attributes >> 24 == PMI_SEGMENT:
            raw = data[seg_offset:seg_offset + length]
            start = raw.find(b'\xfd7zXZ')
            if start < 0:
                raise SystemExit('PMI segment is not LZMA-compressed; not supported by this tool')
            return lzma.LZMADecompressor().decompress(raw[start:])
    return None


def read_entity(reader, strings):
    entity = {'offset': reader.position}
    # PMI 2D Data: base data, text entities, non-text polylines.
    entity['user_label'] = reader.i32()
    frame_flag = reader.u8()
    entity['frame_flag'] = frame_flag
    if frame_flag:
        entity['frame'] = {'origin': reader.unpack('3f'), 'x_axis_point': reader.unpack('3f'), 'y_axis_point': reader.unpack('3f')}
    entity['text_height'] = reader.f32()
    entity['symbol_valid'] = reader.u8()
    text_count = reader.count()
    texts = []
    for _ in range(text_count):
        text = {'string_id': reader.u32(), 'font': reader.i32()}
        reader.i32()
        reader.f32()
        text['box'] = reader.unpack('6f')
        indices = []
        for _ in range(reader.count()):
            indices.append(reader.i16())
        text['polyline_indices'] = indices
        text['polyline_coords'] = reader.vec('f') if indices else []
        if text['string_id'] != NO_STRING and text['string_id'] < len(strings):
            text['string'] = strings[text['string_id']]
        texts.append(text)
    entity['text_entities'] = texts
    indices = [reader.i32() for _ in range(reader.count())]
    types = [reader.i16() for _ in range(reader.count())]
    widths = [reader.i16() for _ in range(reader.count())]
    coords = reader.vec('f')
    entity['polyline'] = {'indices': indices, 'types': types, 'widths': widths, 'vertex_count': len(coords) // 3,
                          'coords': coords}
    # Generic entity fields (order as in the vendored reader, confirmed by the controls).
    properties = []
    for _ in range(reader.count()):
        key, key_hidden = reader.mbstring(), reader.u8()
        value, value_hidden = reader.mbstring(), reader.u8()
        properties.append({'key': key, 'value': value, 'key_hidden': key_hidden, 'value_hidden': value_hidden})
    entity['properties'] = properties
    type_name_id, parent_name_id = reader.u32(), reader.u32()
    entity['type_name'] = strings[type_name_id] if type_name_id < len(strings) else None
    entity['parent_type_name'] = strings[parent_name_id] if parent_name_id < len(strings) else None
    entity['entity_type'] = reader.u16()
    entity['parent_type'] = reader.u16()
    entity['entity_type_name'] = ENTITY_TYPES.get(entity['entity_type'], 'unknown')
    entity['user_flags'] = reader.u16()
    return entity


def decode(blob):
    reader = Reader(blob)
    result = {'decoded_bytes': len(blob)}
    element_length = reader.u32()
    guid = reader.take(16)
    result['element_length'] = element_length
    result['object_type_id'] = guid.hex()
    result['base_type'] = reader.u8()
    result['object_id'] = reader.i32()
    result['version'] = reader.u8()
    result['empty_field'] = reader.i16()
    # Order of the PMI Manager element (JT 10.6, 8.3.1 to 8.3.5): design groups, associations, user attributes, string table, model
    # views. Each count is followed by its data, so the counts are not adjacent when one of the collections is not empty.
    design_groups = reader.count()
    if design_groups:
        raise SystemExit('non-empty design groups: layout not yet validated')
    association_count = reader.count()
    # Each association is five 32-bit words (observed in a Solid Edge export with model views: source data, an owning-entity word,
    # a reason code, destination data, an owning-entity word). The words are kept raw; their meaning is not decoded here.
    associations = [[reader.u32() for _ in range(5)] for _ in range(association_count)]
    user_attributes = reader.count()
    if user_attributes:
        raise SystemExit('non-empty user attributes: layout not yet validated')
    result['counts_before_strings'] = {'design_groups': design_groups, 'associations': association_count,
                                       'user_attributes': user_attributes}
    result['associations_raw'] = associations[:12]
    strings = [reader.mbstring() for _ in range(reader.count())]
    result['string_table'] = strings
    model_views = reader.count()
    result['model_view_count'] = model_views
    views = []
    for _ in range(model_views):
        view = {'eye_direction': reader.unpack('3f'), 'angle': reader.f32(), 'eye_position': reader.unpack('3f'),
                'target_point': reader.unpack('3f'), 'view_angle': reader.unpack('3f'), 'viewport_diameter': reader.f32()}
        reader.f32()
        reader.i32()
        view['active'] = reader.i32()
        view['view_id'] = reader.i32()
        name_id = reader.u32()
        view['name'] = strings[name_id] if name_id < len(strings) else None
        properties = []
        for _ in range(reader.count()):
            key, key_hidden = reader.mbstring(), reader.u8()
            value, value_hidden = reader.mbstring(), reader.u8()
            properties.append({'key': key, 'value': value})
        view['properties'] = properties
        views.append(view)
    result['model_views'] = views
    entities = []
    for _ in range(reader.count()):
        entities.append(read_entity(reader, strings))
    result['entities'] = entities
    result['position_after_entities'] = reader.position
    result['trailing_bytes'] = len(blob) - reader.position
    result['trailing_head'] = blob[reader.position:reader.position + 64].hex()
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('input')
    parser.add_argument('--output', required=True)
    parser.add_argument('--no-coordinates', action='store_true', help='omit polyline coordinate arrays from the output')
    args = parser.parse_args()
    blob = segment_payload(open(args.input, 'rb').read())
    if blob is None:
        print('no PMI segment')
        json.dump({'pmi_segment': False}, open(args.output, 'w'), indent=1)
        return 0
    result = decode(blob)
    if args.no_coordinates:
        for entity in result['entities']:
            entity['polyline'].pop('coords', None)
            for text in entity['text_entities']:
                text.pop('polyline_coords', None)
    json.dump(result, open(args.output, 'w'), indent=1)
    print('version %s, %d strings, %d entities, trailing bytes after entities: %d' % (
        result['version'], len(result['string_table']), len(result['entities']), result['trailing_bytes']))
    for number, entity in enumerate(result['entities']):
        props = {p['key']: p['value'] for p in entity['properties']}
        print('%2d %-24s type %#06x parent %#06x label %-3s vertices %-4d texts %d props %-3d name=%r/%r value=%s' % (
            number, entity['entity_type_name'], entity['entity_type'], entity['parent_type'], entity['user_label'],
            entity['polyline']['vertex_count'], len(entity['text_entities']), len(entity['properties']),
            entity['type_name'], entity['parent_type_name'], props.get('value')))
    return 0


if __name__ == '__main__':
    sys.exit(main())
