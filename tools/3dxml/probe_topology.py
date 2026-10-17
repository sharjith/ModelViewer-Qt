"""Measure sample topology headers and reject simple index-packing hypotheses.

Uses only raw archive bytes. Known count offsets are research anchors, NOT
a general parser. No triangles are synthesized and no renderable mesh is
claimed. Run with python -I.
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import zipfile


# These offsets were measured in the supplied archive, not found by treating
# every FF byte in an opaque payload as a field delimiter.
TARGETS = {
    "ISO_2338_5x25_Zyl_Stift.3DRep": {
        "vertex_count": 268,
        "coarse_counts": [0xB7A, 0xC57, 0xCE8, 0xD58],
        "face_counts": [0xDA8, 0xE24, 0xEA4, 0xEE0, 0xF5C, 0xFDC, 0x101C, 0x1050],
    },
    "Distanzring_1_R.3DRep": {
        "vertex_count": 729,
        "coarse_counts": [0x1C2E, 0x2057, 0x224C, 0x2371, 0x2412, 0x247A],
        "face_counts": [0x24BE, 0x2552, 0x25E6, 0x2692, 0x2744, 0x2906],
    },
}


def count(data, pos):
    if data[pos] == 255:
        return struct.unpack_from("<I", data, pos + 1)[0], pos + 5
    return data[pos], pos + 1


def header_at_count(data, offset):
    """Find an envelope ending exactly at an independently measured count."""
    candidates = []
    for pos in range(max(0, offset - 32), offset - 1):
        if data[pos] != 1 or not 0x41 <= data[pos + 1] <= 0x4F:
            continue
        flags = data[pos + 1]
        cursor = pos + 2
        values = {}
        for bit, name in ((1, "triangles"), (2, "strips"), (4, "fans")):
            values[name] = 0
            if flags & bit:
                values[name], cursor = count(data, cursor)
        if cursor == offset and sum(values.values()) > 0:
            candidates.append((pos, flags, values))
    if len(candidates) != 1:
        raise ValueError(f"ambiguous/missing header at {offset:#x}: {candidates}")
    pos, flags, values = candidates[0]
    index_count, cursor = count(data, offset)
    if 3 * sum(values.values()) > index_count:
        raise ValueError("primitive counts exceed index budget")
    normal = None
    if flags & 8:
        normal = list(struct.unpack_from("<3f", data, cursor))
        cursor += 12
    return {
        "header_offset": pos, "flags_hex": f"{flags:02x}",
        "primitive_counts": values, "count_offset": offset,
        "index_count_candidate": index_count,
        "constant_normal_serialized": normal,
        "opaque_indices_start": cursor,
        # Strip/fan lengths still need to be located. Their sum would need
        # to equal index_count - 3*triangles under this interpretation.
        "strip_fan_index_budget": index_count - 3 * values["triangles"],
        "strip_fan_length_values_expected": values["strips"] + values["fans"],
        "triangle_count_if_no_degenerates": index_count - 2 * sum(values.values()),
    }


def fixed_width_probe(data, vertex_count, index_count, extra_counts):
    """Reject fixed-width absolute index vectors by both size and range.

    Try both byte bit orders and 0..7 leading bits. For strip/fan vectors,
    also try a single flat packed vector including one length per primitive.
    Those lengths are allowed values up to index_count, whereas vertex
    references are bounded by vertex_count. Passing is only a hypothesis.
    """
    results = []
    size_candidates = 0
    for logical_count in sorted({index_count, index_count + extra_counts}):
        for width in range(1, 17):
            for leading in range(8):
                consumed = leading + logical_count * width
                # Every bounded payload observed here is word-aligned.
                if not 0 <= len(data) * 8 - consumed < 32:
                    continue
                for order in ("lsb", "msb", "msb_u32le", "lsb_u32be"):
                    size_candidates += 1
                    if "u32" in order:
                        shifts = range(31, -1, -1) if order.startswith("msb") else range(32)
                        endian = "little" if order.endswith("le") else "big"
                        bits = [(int.from_bytes(data[p:p + 4], endian) >> shift) & 1
                                for p in range(0, len(data), 4) for shift in shifts]
                    else:
                        bits = [(byte >> shift) & 1 for byte in data
                                for shift in (range(8) if order == "lsb" else range(7, -1, -1))]
                    cursor = leading
                    values = []
                    for _ in range(logical_count):
                        part = bits[cursor:cursor + width]
                        value = sum(bit << i for i, bit in enumerate(part)) if order.startswith("lsb") else 0
                        if order.startswith("msb"):
                            for bit in part:
                                value = value * 2 + bit
                        values.append(value)
                        cursor += width
                    outside_range = sum(v >= max(vertex_count, index_count + 1) for v in values)
                    if outside_range or any(bits[cursor:]):
                        continue
                    results.append({
                        "logical_values": logical_count, "width": width,
                        "leading_bits": leading, "order": order,
                        "trailing_bits": bits[cursor:],
                        "zero_tail_padding": not any(bits[cursor:]),
                        "maximum_value": max(values),
                        "values_outside_vertex_range": sum(v >= vertex_count for v in values),
                        "values_outside_vertex_or_length_range": sum(
                            v >= max(vertex_count, index_count + 1) for v in values),
                        "first_values": values[:12],
                    })
    return {"size_candidates_checked": size_candidates,
            "range_and_padding_candidates": results,
            "caution": "passing an individual block does not establish its codec"}


def analyze(path):
    rows = []
    with zipfile.ZipFile(path) as archive:
        for name, target in TARGETS.items():
            data = archive.read(name)
            blocks = []
            offsets = target["coarse_counts"] + target["face_counts"]
            for offset in offsets:
                block = header_at_count(data, offset)
                block["group"] = "coarse" if offset in target["coarse_counts"] else "finest_face"
                blocks.append(block)
            # Bound earlier blocks from an independent following envelope.
            # For coarse blocks a float precedes the recurring style header.
            for i, block in enumerate(blocks[:-1]):
                next_block = blocks[i + 1]
                if block["group"] == "coarse":
                    end = data.rfind(bytes.fromhex("30 04 04 ff ff"),
                                     block["opaque_indices_start"], next_block["header_offset"])
                    if end < 4:
                        raise ValueError("missing style envelope")
                    block["following_scalar"] = struct.unpack_from("<f", data, end - 4)[0]
                    end -= 4
                else:
                    end = next_block["header_offset"]
                block["opaque_indices_end"] = end
                payload = data[block["opaque_indices_start"]:end]
                block["opaque_index_bytes"] = len(payload)
                block["word_aligned_length"] = len(payload) % 4 == 0
                block["fixed_width_probe"] = fixed_width_probe(
                    payload, target["vertex_count"], block["index_count_candidate"],
                    block["strip_fan_length_values_expected"])
            fine = [b for b in blocks if b["group"] == "finest_face"]
            coarse = [b for b in blocks if b["group"] == "coarse"]
            rows.append({
                "member": name, "sha256": hashlib.sha256(data).hexdigest(),
                "status": "primitive interpretation supported; index codec NOT decoded",
                "vertex_count_candidate": target["vertex_count"],
                "coarse_triangle_counts": [b["triangle_count_if_no_degenerates"] for b in coarse],
                "finest_face_triangle_counts": [b["triangle_count_if_no_degenerates"] for b in fine],
                "finest_triangle_total": sum(b["triangle_count_if_no_degenerates"] for b in fine),
                "blocks": blocks,
            })
    return {"method": "sample bytes and explicit measured field anchors only",
            "warning": "No renderable indices; no triangle synthesis; final payload boundaries unresolved",
            "representations": rows}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.resolve() == args.archive.resolve():
        parser.error("output must differ from archive")
    report = analyze(args.archive)
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    for row in report["representations"]:
        print(row["member"], "coarse:", row["coarse_triangle_counts"],
              "finest:", row["finest_face_triangle_counts"],
              "total:", row["finest_triangle_total"])


if __name__ == "__main__":
    main()
