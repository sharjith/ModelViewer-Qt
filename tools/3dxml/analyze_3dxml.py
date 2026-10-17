"""Sample-derived 3DXML research; not a production importer.

Reads ZIP members in memory, never executes or extracts their contents. Uses
only Python's standard library. Offsets are member-relative, not ZIP offsets.
Run with python -I; the JSON output contains measurements, not source geometry.
"""

import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import struct
import zipfile
import xml.etree.ElementTree as ET


MAX_MEMBER = 128 * 1024 * 1024
MAX_ITEMS = 10_000_000
TARGETS = {"ISO_2338_5x25_Zyl_Stift.3DRep", "Distanzring_1_R.3DRep"}


def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def packed(data, width, count):
    mask = (1 << width) - 1
    return [(byte >> shift) & mask for byte in data
            for shift in range(0, 8, width)][:count]


def histogram(values):
    return dict(sorted(Counter(values).items()))


def read_count(data, offset):
    """Observed compact count convention; FF escapes to a little-endian u32."""
    if offset >= len(data):
        raise ValueError("truncated count")
    if data[offset] != 255:
        return data[offset], offset + 1
    if offset + 5 > len(data):
        raise ValueError("truncated extended count")
    return u32(data, offset + 1), offset + 5


def locate_layout(data):
    """Heuristic discovery, then exact size/count constraints. Not a CFV3 parser."""
    for offset in range(5, min(4096, max(0, len(data) - 9))):
        if data[offset - 5:offset] != bytes.fromhex("ff ff 02 00 01"):
            continue
        count, second = read_count(data, offset)
        second_count, prefix_start = read_count(data, second)
        if not 10 < count <= MAX_ITEMS or count != second_count:
            continue
        symbol_start = prefix_start + 7
        if symbol_start > len(data):
            continue
        # Sample-supported selector hypothesis: low bit of prefix byte 3.
        raw_mode = bool(data[prefix_start + 3] & 1)
        if raw_mode:
            symbols = []
            float_header = None
            float_start = symbol_start
            float_count = count * 3
        else:
            float_header = symbol_start + (count + 3) // 4
            if float_header >= len(data):
                continue
            float_count, float_start = read_count(data, float_header)
            symbols = packed(data[symbol_start:float_header], 2, count)
            if sum((symbol + 3) % 4 for symbol in symbols) != float_count:
                continue
        if float_count > MAX_ITEMS:
            continue
        word_header = float_start + float_count * 4
        if word_header >= len(data):
            continue
        word_count, word_start = read_count(data, word_header)
        if word_count > MAX_ITEMS:
            continue
        tags_start = word_start + word_count * 2
        metadata_start = tags_start + (count + 1) // 2
        if metadata_start + 9 > len(data):
            continue
        tags = packed(data[tags_start:metadata_start], 4, count)
        if any(tag > 11 for tag in tags):
            continue
        if 2 * sum(tag < 6 for tag in tags) != word_count:
            continue
        return {
            "count_header": offset, "count": count,
            "position_mode_candidate": "raw" if raw_mode else "prefix-reuse",
            "unknown_prefix_hex": data[prefix_start:symbol_start].hex(),
            "symbols_start": symbol_start, "float_header": float_header,
            "float_start": float_start, "float_count": float_count,
            "word_header": word_header, "word_start": word_start,
            "word_count": word_count, "tags_start": tags_start,
            "metadata_start": metadata_start,
            "metadata_hex": data[metadata_start:metadata_start + 9].hex(),
            "symbols": symbols, "tags": tags,
        }
    return None


def coordinate_candidate(data, layout):
    """Provisional prefix reuse in serialized (z,x,y) order on the two targets."""
    values = struct.unpack_from("<%df" % layout["float_count"], data,
                                layout["float_start"])
    previous = [0.0, 0.0, 0.0]
    points = []
    cursor = 0
    for symbol in layout["symbols"]:
        consume = (symbol + 3) % 4
        if consume:
            previous[3 - consume:] = values[cursor:cursor + consume]
        cursor += consume
        points.append(tuple(previous))
    if cursor != len(values):
        raise ValueError("coordinate consumption mismatch")
    radii = [math.hypot(point[1], point[2]) for point in points]
    result = {
        "status": "provisional; component order and initialization not generalized",
        "serialized_component_order_candidate": "z,x,y",
        "records": len(points), "unique_tuples": len(set(points)),
        "bounds_in_serialized_order": [[min(p[i] for p in points),
                                         max(p[i] for p in points)] for i in range(3)],
        "axial_levels": histogram(point[0] for point in points),
        "radii_rounded_4dp": histogram(round(radius, 4) for radius in radii),
    }
    return result, points


def ring_normal_probe(data, layout, points):
    """Deliberately report failures of a plausible normal mapping, not just fits."""
    words = struct.unpack_from("<%dh" % layout["word_count"], data,
                               layout["word_start"])
    cursor = 0
    errors = []
    bad = []
    cap_mismatches = []
    for index, (point, tag) in enumerate(zip(points, layout["tags"])):
        if tag < 6:
            coefficients = words[cursor:cursor + 2]
            cursor += 2
            # Candidate dominant axes are z,x,y for tag pairs 0/1,2/3,4/5.
            remaining_axes = [axis for axis in range(3) if axis != tag // 2]
            radius = math.hypot(point[1], point[2])
            sign = 1 if radius > 30 else -1
            expected = (0, sign * point[1] / radius, sign * point[2] / radius)
            error = max(abs(value / 32767 - expected[axis])
                        for axis, value in zip(remaining_axes, coefficients))
            errors.append(error)
            if error > 0.001:
                bad.append(index)
        elif tag in (6, 7) and ((tag == 6) != (point[0] > 517)):
            cap_mismatches.append(index)
    return {
        "status": "REJECT as exact decoder: residuals remain",
        "candidate_scale": 32767, "word_consumption": cursor,
        "noncardinal_records": len(errors),
        "maximum_stored_component_error": max(errors),
        "mean_stored_component_error": sum(errors) / len(errors),
        "records_above_0_001_error": bad,
        "cap_sign_mismatch_indices": cap_mismatches,
    }


def read_member(archive, name):
    info = archive.getinfo(name)
    if info.file_size > MAX_MEMBER:
        raise ValueError(f"member exceeds research size limit: {name}")
    return archive.read(info)  # zipfile also verifies the CRC.


def xml_inventory(archive):
    manifest = ET.fromstring(read_member(archive, "Manifest.xml"))
    root_name = next(e.text.strip() for e in manifest.iter()
                     if e.tag.rsplit("}", 1)[-1] == "Root")
    root = ET.fromstring(read_member(archive, root_name))
    counts = Counter(e.tag.rsplit("}", 1)[-1] for e in root.iter())
    matrices = [list(map(float, e.text.split())) for e in root.iter()
                if e.tag.rsplit("}", 1)[-1] == "RelativeMatrix"]
    identity = [1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0]
    rep_links = [e.attrib["associatedFile"] for e in root.iter()
                 if "associatedFile" in e.attrib]
    names = set(archive.namelist())
    return {
        "root_member": root_name, "element_counts": dict(sorted(counts.items())),
        "relative_matrix_lengths": histogram(len(m) for m in matrices),
        "nonidentity_matrix_count": sum(m != identity for m in matrices),
        "associated_file_count": len(rep_links),
        "missing_local_rep_links": [link for link in rep_links
                                    if link.startswith("urn:3DXML:")
                                    and link[len("urn:3DXML:"):] not in names],
    }


def analyze(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    report = {
        "source_filename": path.name, "source_sha256": digest.hexdigest(),
        "method": "raw sample bytes; no vendor code, software, schemas or SDKs",
        "scope": "first recognized layout per member, first 4096 bytes searched",
        "representations": [],
    }
    with zipfile.ZipFile(path) as archive:
        if len(archive.namelist()) != len(set(archive.namelist())):
            raise ValueError("duplicate ZIP names: ambiguous research input")
        report["zip_entry_count"] = len(archive.infolist())
        report["xml"] = xml_inventory(archive)
        for info in archive.infolist():
            if not info.filename.lower().endswith(".3drep"):
                continue
            data = read_member(archive, info.filename)
            row = {"member": info.filename, "size": len(data),
                   "sha256": hashlib.sha256(data).hexdigest(),
                   "prefix_hex": data[:24].hex()}
            layout = locate_layout(data)
            if layout is None:
                row["status"] = "unrecognized by heuristic; not classified as empty"
            else:
                row["status"] = "count/length constraints match"
                row.update({key: value for key, value in layout.items()
                            if key not in ("symbols", "tags")})
                row["symbol_histogram"] = histogram(layout["symbols"])
                row["tag_histogram"] = histogram(layout["tags"])
                if info.filename in TARGETS:
                    row["coordinate_candidate"], points = coordinate_candidate(data, layout)
                    if info.filename == "Distanzring_1_R.3DRep":
                        row["normal_probe"] = ring_normal_probe(data, layout, points)
                    start = layout["metadata_start"] + 9
                    # Raw marker candidates only. FF also occurs inside payloads.
                    row["later_marker_candidates"] = [
                        {"offset": p, "value": u32(data, p + 1)}
                        for p in range(start, min(len(data) - 4, start + 4096))
                        if data[p] == 255 and 0 < u32(data, p + 1) < 100000]
            report["representations"].append(row)
    rows = report["representations"]
    report["summary"] = {
        "rep_members": len(rows),
        "recognized_members": sum(r["status"] == "count/length constraints match" for r in rows),
        "unrecognized_members": [r["member"] for r in rows
                                 if r["status"] != "count/length constraints match"],
        "metadata_prefix_histogram": histogram(r["metadata_hex"][:8]
                                               for r in rows if "metadata_hex" in r),
    }
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.archive.resolve() == args.output.resolve():
        parser.error("output must differ from the source archive")
    report = analyze(args.archive)
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(json.dumps(report["summary"], indent=2))


if __name__ == "__main__":
    main()
