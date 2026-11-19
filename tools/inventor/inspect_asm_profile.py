# SPDX-License-Identifier: GPL-3.0-only
"""Summarize saved ASM body, curve and surface records before the history section."""

import argparse
from collections import Counter
import json
from pathlib import Path
import struct

from verify_part_segment import configure_zstd, reference_payload


ASM_MAGIC = b"ASM BinaryFile4"
NUMBER_FORMATS = {2: "B", 3: "h", 4: "i", 5: "f", 6: "d", 21: "i", 19: "ddd", 20: "ddd", 22: "dd"}
STRING_LENGTHS = {7: "B", 8: "H", 9: "I", 13: "B", 14: "B", 18: "B"}


class Records:
    def __init__(self, data):
        start = data.index(ASM_MAGIC)
        if data.find(ASM_MAGIC, start + len(ASM_MAGIC)) >= 0:
            raise ValueError("More than one ASM header in the saved segment")
        self.data = data
        self.offset = start + len(ASM_MAGIC) + 16

    def take(self, size):
        end = self.offset + size
        if end > len(self.data):
            raise ValueError("Truncated ASM record")
        value = self.data[self.offset:end]
        self.offset = end
        return value

    def read(self, fmt):
        values = struct.unpack("<" + fmt, self.take(struct.calcsize("<" + fmt)))
        return values[0] if len(values) == 1 else values

    def field(self, tag):
        if tag in NUMBER_FORMATS:
            return self.read(NUMBER_FORMATS[tag])
        if tag in STRING_LENGTHS:
            return self.take(self.read(STRING_LENGTHS[tag]))
        if tag == 12:
            return self.read("i")
        if tag in (10, 11, 15, 16):
            return None
        raise ValueError(f"Unknown ASM field tag {tag:#x}")

    def current(self):
        for _ in range(3):
            if self.read("B") != 7:
                raise ValueError("Unexpected ASM header string")
            self.field(7)
        header = []
        for _ in range(3):
            if self.read("B") != 6:
                raise ValueError("Unexpected ASM header real")
            header.append(self.field(6))

        rows = []
        while True:
            name = []
            while True:
                tag = self.read("B")
                if tag not in (13, 14):
                    raise ValueError("Expected an ASM record type")
                name.append(self.field(tag))
                if tag == 13:
                    break
            name = b"-".join(name).decode("ascii")
            if name in ("Begin-of-ASM-History-Data", "End-of-ASM-data"):
                return header[0], rows
            fields = []
            tag = self.read("B")
            while tag != 17:
                fields.append(self.field(tag))
                tag = self.read("B")
            rows.append((name, fields))


def summarize(codec, data):
    scale, rows = Records(data).current()
    def target_type(index):
        if index == -1:
            return "none"
        if not 0 <= index < len(rows):
            raise ValueError("ASM link points outside the current record table")
        return rows[index][0]

    coedge_types = {"coedge", "tcoedge-coedge"}
    edge_types = {"edge", "tedge-edge"}
    edge_uses = Counter(fields[6] for name, fields in rows if name in coedge_types)
    multiply_used = {edge_id: [] for edge_id, count in edge_uses.items() if count > 2}
    for coedge_id, (name, fields) in enumerate(rows):
        if name in coedge_types and fields[6] in multiply_used:
            loop_id = fields[8]
            face_id = rows[loop_id][1][5] if target_type(loop_id) == "loop" else -1
            multiply_used[fields[6]].append({
                "coedge": coedge_id,
                "loop": loop_id,
                "face": face_id if face_id >= 0 else None,
                "surface": target_type(rows[face_id][1][7]) if target_type(face_id) == "face" else None,
                "next_coedge": fields[3],
            })
    points = [fields[3] for name, fields in rows if name == "point"]
    spans = [(max(axis) - min(axis)) * scale for axis in zip(*points)]
    return {
        "compression": codec,
        "millimeters_per_model_unit": scale,
        "bodies": sum(name == "body" for name, _ in rows),
        "face_surfaces": dict(Counter(target_type(fields[7]) for name, fields in rows if name == "face")),
        "edge_curves": dict(Counter(target_type(fields[8]) for name, fields in rows if name in edge_types)),
        "edge_use_histogram": dict(sorted(Counter(edge_uses.values()).items())),
        "multiply_used_edges": multiply_used,
        "point_record_spans_mm": spans,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zstd-library", type=Path, required=True)
    parser.add_argument("--sample", nargs=2, action="append", metavar=("ID", "PATH"), required=True)
    args = parser.parse_args()
    library = configure_zstd(args.zstd_library)
    results = {key: summarize(*reference_payload(Path(path), library)) for key, path in args.sample}
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
