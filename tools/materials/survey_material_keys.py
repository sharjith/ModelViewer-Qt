#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Read-only survey: which material / mass / density attributes do CAD and PLM files declare?

Standard library only; never extracts or executes archive members; prints counts of attribute *names* that
match material, mass, density or weight keywords, plus the sample's SHA-256 and size. It prints no model,
part or company names and no geometry, so its output can be summarised under neutral sample IDs.

  python -I tools/materials/survey_material_keys.py 3dxml  <file.3dxml> [...]
  python -I tools/materials/survey_material_keys.py step   <file.step|stp> [...]
  python -I tools/materials/survey_material_keys.py jtprops <jt_probe --properties output .json> [...]

Output is JSON on stdout; redirect it under out/material-survey/.
"""
import hashlib
import json
import re
import sys
import zipfile
from collections import Counter
from pathlib import Path
import xml.etree.ElementTree as ET

KEYWORDS = re.compile(r'material|mass|density|weight|masse|poids|werkstoff|materiau|matiere|grade|alloy|steel|aluminium|aluminum',
                      re.IGNORECASE)
MAX_MEMBER = 256 * 1024 * 1024


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def local(tag):
    return tag.rsplit('}', 1)[-1]


def survey_3dxml(path):
    report = {'kind': '3dxml', 'bytes': path.stat().st_size, 'sha256': sha256(path)}
    tags, attrs, values = Counter(), Counter(), Counter()
    members = Counter()
    with zipfile.ZipFile(path) as archive:
        for info in archive.infolist():
            suffix = Path(info.filename).suffix.lower()
            members[suffix or '(none)'] += 1
            if suffix not in ('.3dxml', '.xml') or info.file_size > MAX_MEMBER:
                continue
            try:
                root = ET.fromstring(archive.read(info))
            except ET.ParseError:
                continue
            for element in root.iter():
                name = local(element.tag)
                if KEYWORDS.search(name):
                    tags[name] += 1
                for key, value in element.attrib.items():
                    key = local(key)
                    if KEYWORDS.search(key):
                        attrs[f'{name}@{key}'] += 1
                    # attribute *names* declared as <Attr name="Material" value="..."> style pairs
                    if key in ('name', 'Name', 'key') and KEYWORDS.search(value) and len(value) < 64:
                        values[f'{name}[{key}={value}]'] += 1
    report['members_by_suffix'] = dict(members)
    report['elements_matching'] = dict(tags.most_common(60))
    report['attributes_matching'] = dict(attrs.most_common(60))
    report['named_attributes_matching'] = dict(values.most_common(60))
    return report


STEP_ENTITIES = ('MATERIAL_DESIGNATION', 'MATERIAL_PROPERTY', 'MATERIAL_PROPERTY_REPRESENTATION', 'PROPERTY_DEFINITION',
                 'PROPERTY_DEFINITION_REPRESENTATION', 'DESCRIPTIVE_REPRESENTATION_ITEM', 'MASS_MEASURE_WITH_UNIT',
                 'MASS_UNIT', 'DENSITY_MEASURE_WITH_UNIT', 'PRODUCT_DEFINITION', 'PRODUCT',
                 'MEASURE_REPRESENTATION_ITEM', 'GENERAL_PROPERTY', 'GENERAL_PROPERTY_ASSOCIATION',
                 'PRODUCT_DEFINITION_FORMATION', 'ADVANCED_BREP_SHAPE_REPRESENTATION', 'SHELL_BASED_SURFACE_MODEL',
                 'GEOMETRIC_TOLERANCE', 'DIMENSIONAL_SIZE')


def survey_step(path):
    report = {'kind': 'step', 'bytes': path.stat().st_size, 'sha256': sha256(path)}
    schema = None
    counts = Counter()
    names = Counter()
    entity = re.compile(r'^#\d+\s*=\s*([A-Z0-9_]+)\s*\((.*)$')
    quoted = re.compile(r"'([^']{1,60})'")
    with path.open('r', encoding='latin-1', errors='replace') as stream:
        pending = ''
        for line in stream:
            if schema is None and 'FILE_SCHEMA' in line:
                schema = line.strip()[:160]
            line = pending + line.strip()
            if not line.endswith(';') and line.startswith('#'):
                pending = line
                continue
            pending = ''
            match = entity.match(line)
            if not match:
                continue
            kind = match.group(1)
            if kind in STEP_ENTITIES:
                counts[kind] += 1
            if kind in ('PROPERTY_DEFINITION', 'DESCRIPTIVE_REPRESENTATION_ITEM', 'MEASURE_REPRESENTATION_ITEM',
                        'MATERIAL_DESIGNATION', 'GENERAL_PROPERTY', 'MATERIAL_PROPERTY', 'REPRESENTATION'):
                for text in quoted.findall(match.group(2)):
                    if KEYWORDS.search(text):
                        names[f'{kind}:{text}'] += 1
    report['file_schema'] = schema
    report['entity_counts'] = dict(counts)
    report['material_mass_names'] = dict(names.most_common(60))
    return report


def survey_jt_props(path):
    """Aggregates jt_probe --properties JSON (shape unknown in advance): counts every key and key=value pair name."""
    report = {'kind': 'jt-properties', 'file': path.name}
    # the probe prints diagnostics before the JSON document: start at the first line that opens it
    lines = path.read_text(encoding='utf-8', errors='replace').splitlines()
    start = next(i for i, line in enumerate(lines) if line[:1] in '[{')
    data = json.loads('\n'.join(lines[start:]))
    keys, matching, rows, values = Counter(), Counter(), 0, {}

    def walk(node):
        """Counts every {Key, Value} pair; ignores the pair ids and flags."""
        nonlocal rows
        if isinstance(node, dict):
            if 'Key' in node and 'Value' in node:
                rows += 1
                key, value = str(node['Key']), str(node['Value'])
                keys[key] += 1
                if KEYWORDS.search(key) or KEYWORDS.search(value):
                    matching[key] += 1
                    values.setdefault(key, Counter())[value[:60]] += 1
            for child in node.values():
                walk(child)
        elif isinstance(node, list):
            for item in node:
                walk(item)

    walk(data)
    report['key_value_pairs'] = rows
    report['distinct_keys'] = len(keys)
    report['all_keys'] = dict(keys.most_common(80))
    report['keys_matching_material_mass'] = dict(matching.most_common(40))
    report['values_of_matching_keys'] = {k: dict(v.most_common(8)) for k, v in values.items()}
    return report


def main(argv):
    if len(argv) < 3 or argv[1] not in ('3dxml', 'step', 'jtprops'):
        print(__doc__)
        return 2
    function = {'3dxml': survey_3dxml, 'step': survey_step, 'jtprops': survey_jt_props}[argv[1]]
    out = []
    for name in argv[2:]:
        path = Path(name)
        try:
            out.append(function(path))
        except Exception as error:   # report and go on: one bad file must not hide the others
            out.append({'file': path.name, 'error': f'{type(error).__name__}: {error}'})
    json.dump(out, sys.stdout, indent=1)
    print()
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
