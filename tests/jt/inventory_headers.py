# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Inventory authentic JT headers without invoking or qualifying the decoder.

Write the output outside version control: it contains original local paths.
"""
import argparse
import hashlib
import json
import os
from collections import Counter
from pathlib import Path
import re


def inspect(path, hash_contents=False):
    row = {"path": str(path.resolve()), "size": path.stat().st_size}
    with path.open("rb") as stream:
        header = stream.read(80)
        row["header"] = header.decode("ascii", errors="replace").rstrip()
        match = re.match(rb"Version\s+(\d+)\.(\d+)(?=\s)", header)
        if len(header) != 80 or not match:
            row["version"] = None
            row["issue"] = "Missing, short or unrecognized JT header"
        else:
            row["version"] = f"{int(match[1])}.{int(match[2])}"
        if hash_contents:
            digest = hashlib.sha256()
            digest.update(header)
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
            row["sha256"] = digest.hexdigest()
    return row


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", type=Path, help="JT files or folders to scan recursively")
    parser.add_argument("--output", required=True, type=Path, help="Private JSON output path")
    parser.add_argument("--sha256", action="store_true", help="Also hash the complete files")
    args = parser.parse_args()
    paths = {}
    for source in args.inputs:
        if not source.exists():
            parser.error(f"Input does not exist: {source}")
        candidates = source.rglob("*") if source.is_dir() else [source]
        for candidate in candidates:
            if candidate.is_file() and candidate.suffix.lower() == ".jt":
                paths[os.path.normcase(str(candidate.resolve()))] = candidate
    rows = [inspect(paths[key], args.sha256) for key in sorted(paths)]
    counts = Counter(row["version"] or "unrecognized" for row in rows)
    result = {
        "note": "Header inventory only; not decoder qualification or redistribution permission. Paths are private.",
        "version_counts": dict(sorted(counts.items())),
        "files": rows,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Never overwrite an input JT, even if an output path was mistyped.
    if args.output.suffix.lower() == ".jt" or os.path.normcase(str(args.output.resolve())) in paths:
        parser.error("Output path would overwrite an input")
    args.output.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps(result["version_counts"], sort_keys=True))


if __name__ == "__main__":
    main()
