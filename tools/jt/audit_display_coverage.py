#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Read-only JT display coverage inventory; optional pinned reference collection.

This does not load a JT model, execute a decoder, build or test the application.
All output, including optional reference snapshots/page renders, stays in out/.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import urllib.request

REPO=Path(__file__).resolve().parents[2]
COMMIT='27a12d1970e7684ef369d0a815aee0b7a194b02b'
FILES=['TopoMeshCompressedRepDataV1.java','TopoMeshCompressedLODData.java',
       'VertexShapeLODData.java','PolylineSetShapeLODElement.java','PointSetShapeLODElement.java']
TYPES={
    'polyline_node':'10dd1046-2ac8-11d1-9b-6b-00-80-c7-bb-59-97',
    'polyline_lod':'10dd10a1-2ac8-11d1-9b-6b-00-80-c7-bb-59-97',
    'point_node':'98134716-0010-0818-19-98-08-00-09-83-5d-5a',
    'point_lod':'98134716-0011-0818-19-98-08-00-09-83-5d-5a',
}


def allowed(path):
    path=path.resolve()
    if not path.is_relative_to(REPO/'out'):raise ValueError('Output must stay under ignored out/')
    return path


def snapshot(folder):
    folder=allowed(folder);folder.mkdir(parents=True,exist_ok=True)
    manifest=dict(repository='https://github.com/Liamsi/jcadlib',commit=COMMIT,
        method='Reference-only reading; no build/execution or source incorporation',files=[])
    for name in FILES:
        url=f'https://raw.githubusercontent.com/Liamsi/jcadlib/{COMMIT}/src/de/raida/jcadlib/cadimport/jt/model/{name}'
        request=urllib.request.Request(url,headers={'User-Agent':'ModelViewer-source-research'})
        with urllib.request.urlopen(request,timeout=25) as response:data=response.read(1024*1024+1)
        if len(data)>1024*1024:raise ValueError('Reference file exceeds collection limit')
        if b'The MIT License' not in data:raise ValueError('Expected reference notice missing; review source before use')
        (folder/name).write_bytes(data)
        manifest['files'].append(dict(file=name,url=url,sha256=hashlib.sha256(data).hexdigest()))
    (folder/'provenance.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')


def render(folder):
    import fitz
    folder=allowed(folder);folder.mkdir(parents=True,exist_ok=True)
    for stem,figures in [('jt10-reference',['Figure 41','Figure 85','Figure 87','Figure 91']),
                         ('jt95-reference',['Figure 91:','Figure 90:'])]:
        with fitz.open(REPO/'out/jt-research'/f'{stem}.pdf') as document:
            for figure in figures:
                pages=[i for i,page in enumerate(document) if i>30 and figure in page.get_text()]
                if not pages:raise ValueError('Missing reference figure '+figure)
                filename=stem+'-'+figure.replace(' ','_').replace(':','')+'.png'
                document[pages[-1]].get_pixmap(matrix=fitz.Matrix(1.4,1.4)).save(folder/filename)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--fetch-reference-source',action='store_true')
    parser.add_argument('--render-references',action='store_true')
    args=parser.parse_args();output=allowed(args.output)
    reference_folder=REPO/'out/jt-research/c3a-reference-review'
    if args.fetch_reference_source:snapshot(reference_folder/'jcadlib')
    if args.render_references:render(reference_folder)
    registrations={}
    for base in ['src/ThirdParty/occt-jt','src/Import/Jt']:
        for path in (REPO/base).rglob('*'):
            if path.suffix not in ('.cxx','.cpp'):continue
            source=path.read_text(encoding='utf-8',errors='replace')
            for item in re.finditer(r'IMPLEMENT_OBJECT_CLASS\s*\(\s*(\w+)\s*,\s*"[^"]*"\s*,\s*"([^"]+)"',source,re.S):
                registrations[item[2].lower()]=dict(type=item[1],file=path.relative_to(REPO).as_posix())
    probes=[
        ('jt_triangle_validation','src/Import/Jt/JtSceneBuilder.cpp','if(f.mNumIndices!=3 || !f.mIndices)'),
        ('jt_triangle_copy','src/Import/Jt/JtSceneBuilder.cpp','copy->mFaces[i].mNumIndices=3'),
        ('jt_triangle_palette_subset','src/Import/Jt/JtSceneBuilder.cpp','for(auto face:faces)for(unsigned j=0;j<3;'),
        ('jt_topomesh_todo','src/ThirdParty/occt-jt/JtElement/JtElement_ShapeLOD_Vertex.cxx','TODO: Read TopoMesh Compressed Rep Data'),
        ('generic_point_import','src/Import/AssImpModelLoader.cpp','return GL_POINTS;'),
        ('generic_line_import','src/Import/AssImpModelLoader.cpp','return GL_LINES;'),
        ('generic_indexed_draw','src/Scene/SceneMesh.cpp','glDrawElements(_primitiveMode,'),
        ('generic_primitive_export','src/Scene/SceneGraphExporter.cpp','primitiveModeToAiPrimitiveType(mesh->getPrimitiveMode())'),
        ('raytracer_triangle_gate','src/RayTracing/RtSceneBuilder.cpp','mesh->getPrimitiveMode() != GL_TRIANGLES'),
    ]
    facts=[]
    for name,filename,needle in probes:
        data=(REPO/filename).read_bytes();source=data.decode('utf-8',errors='replace')
        facts.append(dict(id=name,file=filename,sha256=hashlib.sha256(data).hexdigest(),
            lines=[i for i,line in enumerate(source.splitlines(),1) if needle in line]))
    references=[]
    for name in ['jt10-reference.pdf','jt95-reference.pdf']:
        path=REPO/'out/jt-research'/name
        references.append(dict(id=name,available=path.is_file(),sha256=hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None))
    manifest=reference_folder/'jcadlib/provenance.json'
    report=dict(method='Read-only source registration/assumption inventory; not native/import/visual acceptance',
        registered_class_count=len(registrations),
        display_types={name:dict(guid=guid,registration=registrations.get(guid)) for name,guid in TYPES.items()},
        source_facts=facts,references=references,
        external_reference=json.loads(manifest.read_text()) if manifest.is_file() else None,
        evidence_gaps=['Authentic line/point payload and expected primitive boundaries',
                       'Family-specific nested envelopes/count and predictor qualification',
                       'No C3 native/build/viewer/export acceptance'])
    output.parent.mkdir(parents=True,exist_ok=True);output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(f'Inventory saved: {sum(v["registration"] is not None for v in report["display_types"].values())}/4 display types registered; {len(facts)} source probes. No decoder/build execution.')


if __name__=='__main__':main()
