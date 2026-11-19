# SPDX-License-Identifier: GPL-3.0-only
"""Count candidate Inventor references present in a local assembly package."""

import argparse
from collections import Counter
import json
from pathlib import Path, PureWindowsPath
import struct

import olefile


CAD_EXTENSIONS = {".ipt", ".iam"}


def saved_paths(data):
    offset = 0
    while offset + 4 <= len(data):
        length = struct.unpack_from("<I", data, offset)[0]
        start = offset + 4
        stop = start + 2 * length
        offset += 1
        if not 5 <= length <= 1000 or stop > len(data):
            continue
        try:
            raw = data[start:stop].decode("utf-16-le")
        except UnicodeDecodeError:
            continue
        path = PureWindowsPath(raw)
        if path.suffix.casefold() in CAD_EXTENSIONS and not any(ord(char) < 32 for char in raw):
            yield path
            offset = stop


def reference_status(path, container, local_names):
    parts = path.name.split("*")
    if len(parts) >= 4 and parts[-3].casefold() == "localdocs":
        if container.exists(["LocalDocs", parts[-2]]):
            return "embedded"
        return "missing"
    matches = local_names[path.name.casefold()]
    if matches == 0:
        return "missing"
    return "unique_basename" if matches == 1 else "ambiguous_basename"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path)
    args = parser.parse_args()
    files = [
        path for path in args.folder.rglob("*")
        if path.is_file() and path.suffix.casefold() in CAD_EXTENSIONS
        and all(part.casefold() != "oldversions" for part in path.relative_to(args.folder).parts[:-1])
    ]
    assemblies = sorted((p for p in files if p.suffix.casefold() == ".iam"),
                        key=lambda p: (-p.stat().st_size, p.relative_to(args.folder).as_posix()))
    if not assemblies:
        parser.error("No assembly files found")
    local_names = Counter(path.name.casefold() for path in files)
    observations = []
    for number, assembly in enumerate(assemblies, 1):
        with olefile.OleFileIO(str(assembly), raise_defects=olefile.DEFECT_INCORRECT) as container:
            if not container.exists(["UFRxDoc"]):
                observations.append({"assembly_by_size": number, "status": "missing_ufrxdoc"})
                continue
            with container.openstream(["UFRxDoc"]) as stream:
                paths = list(saved_paths(stream.read()))
            identified = bool(paths) and paths[0].name.casefold() == assembly.name.casefold()
            statuses = Counter(reference_status(path, container, local_names)
                               for path in (paths[1:] if identified else paths))
            observations.append({
                "assembly_by_size": number,
                "self_path_recognized": identified,
                "candidate_counts": dict(sorted(statuses.items())),
            })
    print(json.dumps(observations, indent=2))


if __name__ == "__main__":
    main()
