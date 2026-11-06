# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Check decoder manifest coverage and retained source notices, not legal clearance."""
from pathlib import Path
import json
import re

root = Path(__file__).resolve().parents[2]
decoder = root / "src/ThirdParty/occt-jt"
headers = root / "include/ThirdParty/occt-jt"
manifest = json.loads((decoder / "PROVENANCE.json").read_text(encoding="utf-8"))
records = manifest["files"]
paths = [record["file"] for record in records]
assert len(paths) == len(set(paths)), "Duplicate provenance records"
assert manifest["file_path_base"] == "repository-root", "Unexpected manifest path base"
assert not list(decoder.rglob("*.hxx")), "Decoder headers belong under include/ThirdParty"
assert not list(headers.rglob("*.cxx")), "Decoder implementations belong under src/ThirdParty"
actual = {p.relative_to(root).as_posix() for folder in (decoder, headers)
          for p in folder.rglob("*") if p.suffix in (".cxx", ".hxx")}
assert set(paths) == actual, f"Manifest/source mismatch: {set(paths) ^ actual}"
counts = {"upstream-derived": 0, "ModelViewer-original": 0}
for record in records:
    path = root / record["file"]
    assert any(path.resolve().is_relative_to(folder.resolve()) for folder in (decoder, headers)), "Provenance path outside decoder"
    source = path.read_text(encoding="utf-8")
    origin = record["origin"]
    counts[origin] += 1
    if origin == "upstream-derived":
        assert "OPEN CASCADE SAS" in source and "any later" in source, str(path)
        assert re.fullmatch(r"[0-9a-f]{64}", record["upstream_sha256"]), str(path)
        assert record["upstream_path"].startswith("TKJT/"), str(path)
        if record["modified"]:
            assert record["notice_date"] in source and "Modified for ModelViewer-Qt" in source, str(path)
    else:
        assert "GPL-3.0-only" in source, str(path)
print(f"PASS: {len(records)} decoder files covered; {counts['upstream-derived']} retained upstream notices; {counts['ModelViewer-original']} original additions")
