# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Content-addressed shared resources for original texture visual controls.

JT roots live beside shared glTF references so all resources remain beneath the
JT assembly root. Reports stay in the requested gate folder; generated payloads
stay in its sibling texture-common-ui-fixtures folder, all inside ignored out/.
"""
import copy
import hashlib
import io
import json
from pathlib import Path


class TextureFixtureStore:
    def __init__(self, gate):
        self.gate=gate.resolve()
        self.root=self.gate.parent/'texture-common-ui-fixtures'
        allowed=Path(__file__).resolve().parents[2]/'out'
        if not self.root.is_relative_to(allowed):raise ValueError('Shared fixtures must stay under ignored out/')
        self.gate.mkdir(parents=True,exist_ok=True)
        (self.root/'assets').mkdir(parents=True,exist_ok=True)
        self.prefix=self.gate.name.removesuffix('-ui-fixtures')
        self.references={}

    def asset(self,data,suffix):
        name='assets/'+hashlib.sha256(data).hexdigest()+suffix.lower()
        path=self.root/name
        if not path.exists():path.write_bytes(data)
        elif path.read_bytes()!=data:raise ValueError('Shared asset hash collision')
        return name

    def file(self,path):
        path=Path(path);return self.asset(path.read_bytes(),path.suffix)

    def image(self,image):
        data=io.BytesIO();image.save(data,format='PNG');return self.asset(data.getvalue(),'.png')

    def document(self,doc,base,label,image_overrides=None):
        doc=copy.deepcopy(doc)
        for field in ('buffers','images'):
            for item in doc.get(field,[]):
                uri=item.get('uri')
                if uri and not uri.startswith('data:'):item['uri']=self.file(base/uri)
        for index,uri in (image_overrides or {}).items():doc['images'][index]['uri']=uri
        return self.reference(doc,label)

    def reference(self,doc,label):
        data=(json.dumps(doc,sort_keys=True,indent=2)+'\n').encode()
        name='reference_'+hashlib.sha256(data).hexdigest()+'.gltf'
        path=self.root/name
        if not path.exists():path.write_bytes(data)
        elif path.read_bytes()!=data:raise ValueError('Shared reference hash collision')
        self.references[label]=name
        return name

    def control(self,label,data):
        name=self.prefix+'_'+label.replace('/','_')
        (self.root/name).write_bytes(data)
        return {'file':name,'sha256':hashlib.sha256(data).hexdigest()}

    def cleanup(self,paths):
        # Only named generated files in this gate may be removed. No recursive
        # deletion, source-folder traversal or unrelated output cleanup.
        for path in paths:
            path=Path(path)
            if not path.resolve().is_relative_to(self.gate):raise ValueError('Cleanup escaped the gate folder')
            if path.is_file():path.unlink()

    def report(self,report):
        report.update(shared_folder=str(self.root),references=self.references)
        (self.gate/'authoring.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
