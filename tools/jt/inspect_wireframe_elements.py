#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Read-only element-GUID inventory of the LSG (1), shape (7) and Wireframe Rep (18) segments of a little-endian JT10 file.

Handles the LZMA (flag 3, algorithm 3) segment compression that inspect_jt_structure.py does not scan. Reports the
Wireframe Rep (873a70d0-...) declared edge and MCS curve counts. No geometry is exported. Output stays under out/.
"""
import argparse
import hashlib
import json
import lzma
import struct
import uuid
from collections import Counter
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
WIREFRAME = "873a70d0-2ac8-11d1-9b6b-0080c7bb5997"
END = "ffffffff-ffff-ffff-ffff-ffffffffffff"


def inspect(path):
    data = path.read_bytes()
    toc = struct.unpack_from("<q", data, 85)[0]
    count = struct.unpack_from("<i", data, toc)[0]
    report = {"sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data), "segments": []}
    for index in range(count):
        offset, length, _ = struct.unpack_from("<qiI", data, toc + 20 + 32 * index)
        if offset < 0 or offset + length > len(data):
            raise ValueError("segment outside file")
        kind, size = struct.unpack_from("<ii", data, offset + 16)
        if size != length:
            raise ValueError("segment length mismatch")
        if kind not in (1, 7, 18):
            continue
        if kind in (1, 18):
            flag, packed_size, algorithm = struct.unpack_from("<iiB", data, offset + 24)
            if not 1 <= packed_size <= length - 32:
                raise ValueError("bad packed size")
            packed = data[offset + 33:offset + 32 + packed_size]
            if (flag, algorithm) == (3, 3):
                decoder = lzma.LZMADecompressor(memlimit=128 * 1024 * 1024)
                body = decoder.decompress(packed, max_length=16 * 1024 * 1024)
                if not decoder.eof or decoder.unused_data:
                    raise ValueError("LZMA stream did not end cleanly")
            elif flag == 0:
                body = packed
            else:
                raise ValueError(f"unsupported compression {flag}/{algorithm}")
        else:
            body = data[offset + 24:offset + length]
        elements, position, ends = [], 0, 0
        while position + 20 <= len(body):
            element_length = struct.unpack_from("<i", body, position)[0]
            if element_length < 16 or position + 4 + element_length > len(body):
                break
            guid = str(uuid.UUID(bytes_le=body[position + 4:position + 20]))
            element = {"guid": guid, "length": element_length}
            if guid == END:
                ends += 1
            elif guid == WIREFRAME:
                element["local_version"] = body[position + 25]
                element["edge_count"] = struct.unpack_from("<i", body, position + 26)[0]
                element["mcs_curve_count"] = struct.unpack_from("<i", body, position + 30)[0]
            elements.append(element)
            position += 4 + element_length
            if kind == 1 and ends == 2:
                break
        report["segments"].append({"segment_type": kind, "decoded_bytes": len(body),
                                   "remaining_bytes": len(body) - position, "elements": elements})
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(REPO / "out"):
        raise SystemExit("Output must stay under the ignored out/ folder")
    report = inspect(args.input)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    for segment in report["segments"]:
        counts = dict(Counter(e["guid"] for e in segment["elements"]))
        print(f"segment {segment['segment_type']}: {segment['decoded_bytes']} bytes; {counts}")
        for element in segment["elements"]:
            if "edge_count" in element:
                print(f"  Wireframe Rep: local version {element['local_version']}, edges {element['edge_count']}, "
                      f"MCS curves {element['mcs_curve_count']}")


if __name__ == "__main__":
    main()
