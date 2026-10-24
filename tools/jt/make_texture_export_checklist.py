#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Prepare C2aa GUI export paths from original shared controls; no importer run."""
import argparse
import hashlib
import json
from pathlib import Path


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('affine_folder',type=Path)
    parser.add_argument('output_folder',type=Path)
    args=parser.parse_args()
    root=Path('out').resolve();target=args.output_folder.resolve()
    if not target.is_relative_to(root):raise ValueError('Output must stay under ignored out/')
    report=json.loads((args.affine_folder/'authoring.json').read_text())
    shared=Path(report['shared_folder']).resolve()
    if not shared.is_relative_to(root):raise ValueError('Use original shared controls under out/')
    rows=[('blend_linear','10_0_le'),('blend_trilinear','10_0_le'),
          ('decal_rgb_trilinear','9_0_be'),('ring_rgba','10_0_le'),
          ('ring_luminance_alpha','9_5_be'),('ring_alpha','10_0_le'),('ring_bgra','10_0_le')]
    target.mkdir(parents=True,exist_ok=True)
    cases=[]
    for label,family in rows:
        source=shared/f'texture-affine-filter_{label}_{family}.jt'
        reference=shared/report['references'][label]
        # Small descriptive document aliases share the original buffers/images.
        # Hashes belong in the manifest, not in the owner's visual checklist.
        named_reference=shared/f'reference_{label}.gltf'
        reference_bytes=reference.read_bytes()
        if named_reference.exists() and named_reference.read_bytes()!=reference_bytes:
            raise ValueError(f'Different named reference already exists: {named_reference.name}')
        named_reference.write_bytes(reference_bytes)
        cases.append(dict(id=label,source=str(source),reference=str(named_reference),
                          reference_sha256=hashlib.sha256(reference_bytes).hexdigest()))
    cases.append(dict(id='disk_reference',source=cases[0]['reference'],reference=cases[0]['reference']))
    for case in cases:
        case['source_sha256']=hashlib.sha256(Path(case['source']).read_bytes()).hexdigest()
        case['exports']={fmt:str(target/(case['id']+'.'+fmt)) for fmt in ('glb','gltf')}
    (target/'checklist.json').write_text(json.dumps(dict(cases=cases,method='Original synthetic controls; GUI File > Export, then read-only image/carrier comparison. No exporter execution during checklist creation.'),indent=2)+'\n')
    (target.parent/'c2aa-visual-files.json').write_text(json.dumps(list(dict.fromkeys(c['source'] for c in cases)),indent=2)+'\n')
    print(f'Prepared {len(cases)} source documents, {2*len(cases)} GUI exports; no geometry/image copies')


if __name__=='__main__':main()
