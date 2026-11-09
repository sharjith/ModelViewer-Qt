#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Package the original JT diagnostic tool only; never builds ModelViewer.
Requires PyInstaller in the invoking environment. Generated output stays in out/.
"""
import argparse
import hashlib
import importlib.metadata
import json
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile
import inspect_jt_structure


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,default=Path('out/jt-diagnostics-package'))
    parser.add_argument('--reuse-build',action='store_true',help='Repackage the existing binary without rebuilding')
    args=parser.parse_args();repo=Path(__file__).resolve().parents[2];output=args.output.resolve()
    if not output.is_relative_to(repo/'out'):raise ValueError('Package output must stay inside ignored out/')
    output.mkdir(parents=True,exist_ok=True);source=Path(__file__).with_name('inspect_jt_structure.py')
    registry=output/'jt_type_registry.json';registry.write_text(json.dumps({'types':inspect_jt_structure.known_types()},indent=2)+'\n',encoding='utf-8')
    command=[sys.executable,'-m','PyInstaller','--clean','--noconfirm','--onefile','--windowed','--name','JTDiagnostics',
             '--distpath',str(output/'dist'),'--workpath',str(output/'work'),'--specpath',str(output),
             '--add-data',str(registry)+':.',str(source)]
    if args.reuse_build:
        previous=json.loads((output/'packaging.json').read_text(encoding='utf-8'))
        if previous['diagnostic_source_sha256']!=hashlib.sha256(source.read_bytes()).hexdigest():
            raise ValueError('Diagnostic source changed; rebuild instead of reusing a stale binary')
    else:subprocess.run(command,check=True,cwd=repo)
    package=output/'release';package.mkdir(exist_ok=True)
    executable=package/'JTDiagnostics.exe';shutil.copyfile(output/'dist/JTDiagnostics.exe',executable)
    (package/'README.txt').write_text('JT Diagnostics for Windows x64\n\nDouble-click JTDiagnostics.exe. Select the failing JT files, then choose a report folder.\nSend the resulting JT-report-*.json files and the ModelViewer build/commit identity.\n\nNo Python installation or ModelViewer rebuild is needed on the target PC.\nInputs are opened read-only; reports contain structural counts/type identifiers, optional file hash,\nand no geometry, model names or input paths. External partitions are not followed.\nLarge files may take time while the file hash is read.\n\nThis is an unsigned diagnostic utility, not a geometry converter or conformance validator.\n',encoding='utf-8')
    sources=package/'source';sources.mkdir(exist_ok=True)
    for path in (source,Path(__file__)):shutil.copyfile(path,sources/path.name)
    licenses=package/'licenses';licenses.mkdir(exist_ok=True)
    for path in repo.glob('LICENSE*'):
        if path.is_file():shutil.copyfile(path,licenses/('ModelViewer-'+path.name))
    for pattern in ('LICENSE*','Lib/tkinter/license*','tcl/tcl8.6/license*','tcl/tk8.6/license*'):
        for path in Path(sys.base_prefix).glob(pattern):
            if path.is_file():shutil.copyfile(path,licenses/('Python-'+str(path.relative_to(sys.base_prefix)).replace('\\','-').replace('/','-')))
    # Some Python Windows distributions omit Tcl's separate licence file. Supply
    # the matching public Tcl licence locally in the ignored packaging directory.
    tcl_license=output/'Tcl-license.terms'
    if tcl_license.is_file():shutil.copyfile(tcl_license,licenses/'Tcl-license.terms')
    distribution=importlib.metadata.distribution('pyinstaller')
    for item in distribution.files or []:
        if 'licenses' in item.parts and distribution.locate_file(item).is_file():
            shutil.copyfile(distribution.locate_file(item),licenses/('PyInstaller-'+item.name))
    report={'python':sys.version,'pyinstaller':distribution.version,'diagnostic_source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
            'exe_sha256':hashlib.sha256(executable.read_bytes()).hexdigest(),'registered_types':len(inspect_jt_structure.known_types()),
            'scope':'Owner-requested standalone diagnostic packaging only; no ModelViewer build'}
    (output/'packaging.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    (package/'packaging.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    with zipfile.ZipFile(output/'JTDiagnostics-Windows-x64.zip','w',zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(package.rglob('*')):
            if path.is_file():archive.write(path,path.relative_to(package))
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
