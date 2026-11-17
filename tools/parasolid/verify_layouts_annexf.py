#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Check which entries of a local base-layout file are present in Annex F of the JT 10.6 reference.

Only field NAMES and their ORDER are compared, using the struct definitions printed in the
annex. A layout entry is "verified" when every one of its fields appears in the annex
struct (geometry structs inherit the fields of the struct they derive from), in the same
order; the annex may list additional, newer fields. The annex text is read from a local
file; nothing from it is written out except names and verdicts.

Usage:
    python -I tools/parasolid/verify_layouts_annexf.py LAYOUTS.txt ANNEX_TEXT.txt > out/parasolid-research/annexf-verdicts.json

ANNEX_TEXT.txt is the text extraction of the JT reference PDF.
"""
import json
import re
import sys

STRUCT = re.compile(r"struct (\w+?)_s\s*(?:==\s*(\w+?)_s)?\s*(?://[^\n]*)?\s*\n\s*\{(.*?)\};", re.S)
FIELD = re.compile(r"(\w+)\s*(?:\[[^\]]*\])*\s*;\s*//")


def annex_structs(text):
    body = text[text.find("XT B- Rep data segment"):]
    found = {}
    # The extracted text breaks declarations across lines, so fields are recognised as
    # "<identifier>;" followed by a "// $..." marker, up to the closing "};" of the struct.
    for start in re.finditer(r"struct \w+?_s\s*(?:==|//)", body):
        m = STRUCT.match(body, start.start())
        if not m:
            continue
        fields = [f.lower() for f in FIELD.findall(m.group(3))]
        if m.group(1) not in found or len(fields) > len(found[m.group(1)][1]):
            found[m.group(1)] = (m.group(2), fields)
    return found


def expand(name, found):
    base, fields = found[name]
    return (expand(base, found) if base in found else []) + fields


def main():
    layouts = {}
    for line in open(sys.argv[1], encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        parts = line.split()
        layouts[int(parts[0])] = (parts[1], [f.split(":")[0].lower() for f in parts[2:]])
    found = annex_structs(open(sys.argv[2], encoding="utf-8", errors="replace").read())
    verdicts = {}
    for t, (name, fields) in sorted(layouts.items()):
        if name not in found:
            verdicts[t] = {"name": name, "verdict": "not-found-in-annex"}
            continue
        annex = expand(name, found)
        it = iter(annex)
        ok = all(any(f == a for a in it) for f in fields)
        verdicts[t] = {"name": name, "verdict": "verified" if ok else "differs",
                       "annex_extra": [a for a in annex if a not in fields]}
    print(json.dumps(verdicts, indent=1))


if __name__ == "__main__":
    main()
