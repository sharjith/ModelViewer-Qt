"""Constrained integer-code experiments on measured triangle-only payloads.

Research only. A candidate must explain every selected complete block before
it is reported as a common fit. No vendor implementation or API is used.
"""

import argparse
import itertools
import json
from pathlib import Path
import zipfile


SELECTOR_TABLES = sorted(set(
    table for widths in (
        (0, 0, 8, 16), (0, 1, 8, 16), (0, 4, 8, 16),
        (0, 4, 8, 10), (0, 4, 8, 9), (0, 2, 4, 9),
        (0, 2, 4, 10), (1, 2, 4, 8),
        (0, 8), (0, 9), (0, 10), (1, 8), (1, 9), (1, 10),
        (4, 8), (4, 9), (4, 10), (8, 9), (8, 10), (9, 10),
    ) for table in itertools.permutations(widths)))


class Bits:
    def __init__(self, data, order, leading):
        if order in ("lsb", "msb"):
            shifts = range(8) if order == "lsb" else range(7, -1, -1)
            self.bits = [(byte >> shift) & 1 for byte in data for shift in shifts]
        else:
            shifts = range(32) if order.startswith("lsb") else range(31, -1, -1)
            endian = "big" if order.endswith("be") else "little"
            self.bits = [(int.from_bytes(data[p:p + 4], endian) >> shift) & 1
                         for p in range(0, len(data), 4) for shift in shifts]
        self.little = order.startswith("lsb")
        self.pos = leading

    def bit(self):
        value = self.bits[self.pos]
        self.pos += 1
        return value

    def read(self, width):
        if self.pos + width > len(self.bits):
            raise IndexError("truncated bits")
        value = 0
        for i in range(width):
            bit = self.bit()
            value = value | (bit << i) if self.little else value * 2 + bit
        return value

    def unary(self, terminal):
        value = 0
        while self.bit() != terminal:
            value += 1
        return value


def integer(stream, family, parameter, variant, bound):
    if family == "selectors":
        table = SELECTOR_TABLES[parameter]
        selector = stream.read((len(table) - 1).bit_length())
        value = stream.read(table[selector])
        if variant == 1:
            value += sum(1 << width for width in table[:selector])
        elif variant == 2 and table[selector] == 0:
            value = selector
        return value
    if family == "rice":
        if variant & 2:
            remainder = stream.read(parameter)
            quotient = stream.unary(variant & 1)
        else:
            quotient = stream.unary(variant & 1)
            remainder = stream.read(parameter)
        return (quotient << parameter) | remainder
    if family == "exp_golomb":
        zeros = stream.unary(variant)
        if zeros + parameter > 30:
            raise ValueError("oversize code")
        return ((1 << zeros) - 1) * (1 << parameter) + stream.read(zeros + parameter)
    if family == "bit_groups":
        value = shift = 0
        while True:
            if variant & 2:
                payload = stream.read(parameter)
                more = stream.bit()
            else:
                more = stream.bit()
                payload = stream.read(parameter)
            value |= payload << shift
            shift += parameter
            if more == (variant & 1):
                return value
            if shift > 30:
                raise ValueError("oversize group integer")
    if family == "length_prefix":
        width = stream.read(parameter) + variant
        if width > 30:
            raise ValueError("oversize length prefix")
        return stream.read(width)
    if family == "truncated_binary":
        # This family is usually MSB-oriented. Both orientations are tested
        # explicitly rather than assuming the sample uses a standard variant.
        width = (bound - 1).bit_length()
        threshold = (1 << width) - bound
        value = stream.read(width - 1)
        return value if value < threshold else value * 2 + stream.bit() - threshold
    raise ValueError("unknown family")


def configurations():
    for family, parameters, variants in (
        ("rice", range(10), range(4)),
        ("exp_golomb", range(9), range(2)),
        ("bit_groups", range(3, 10), range(4)),
        ("length_prefix", range(3, 6), range(2)),
        ("truncated_binary", (0,), (0,)),
        ("selectors", range(len(SELECTOR_TABLES)), range(3)),
    ):
        for parameter, variant in itertools.product(parameters, variants):
            for order, leading in itertools.product(("lsb", "msb", "msb_u32le", "lsb_u32be"), range(8)):
                yield family, parameter, variant, order, leading


def evaluate(data, count, bound, config):
    family, parameter, variant, order, leading = config
    stream = Bits(data, order, leading)
    try:
        values = [integer(stream, family, parameter, variant, bound) for _ in range(count)]
    except (IndexError, ValueError):
        return None
    # The whole payload consists of words, so at most the final word may
    # contain padding. Never accept a decoder that leaves another word.
    remaining = len(stream.bits) - stream.pos
    if not 0 <= remaining < 32:
        return None
    transforms = []
    if all(0 <= value < bound for value in values):
        transforms.append("absolute_zero_based")
    if all(1 <= value <= bound for value in values):
        transforms.append("absolute_one_based")
    signed = [(v >> 1) if not v & 1 else -((v >> 1) + 1) for v in values]
    for lanes, initial in itertools.product((1, 3), (0, bound - 1)):
        previous = [initial] * lanes
        valid = True
        for i, value in enumerate(signed):
            lane = i % lanes
            previous[lane] += value
            if not 0 <= previous[lane] < bound:
                valid = False
                break
        if valid:
            transforms.append(f"zigzag_delta_lanes_{lanes}_initial_{initial}")
    return {"bits_consumed": stream.pos, "padding_bits": remaining,
            "zero_padding": not any(stream.bits[stream.pos:]),
            "maximum_integer": max(values), "transforms": transforms,
            "first_integers": values[:12]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--topology-report", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.resolve() in (args.archive.resolve(), args.topology_report.resolve()):
        parser.error("output must differ from inputs")
    report = json.loads(args.topology_report.read_text(encoding="utf-8"))
    blocks = []
    with zipfile.ZipFile(args.archive) as archive:
        for row in report["representations"]:
            data = archive.read(row["member"])
            for block in row["blocks"]:
                counts = block["primitive_counts"]
                if not counts["triangles"] or counts["strips"] or counts["fans"] or "opaque_indices_end" not in block:
                    continue
                blocks.append({"member": row["member"], "offset": block["count_offset"],
                               "count": block["index_count_candidate"],
                               "bound": row["vertex_count_candidate"],
                               "data": data[block["opaque_indices_start"]:block["opaque_indices_end"]]})
    fits = []
    checked = 0
    per_block = [[] for _ in blocks]
    for config in configurations():
        checked += 1
        outcomes = [evaluate(b["data"], b["count"], b["bound"], config) for b in blocks]
        for i, outcome in enumerate(outcomes):
            if outcome and outcome["transforms"]:
                per_block[i].append({"configuration": config, **outcome})
        if all(outcome and outcome["transforms"] for outcome in outcomes):
            common = set(outcomes[0]["transforms"])
            for outcome in outcomes[1:]:
                common.intersection_update(outcome["transforms"])
            if common:
                fits.append({"configuration": config, "common_transforms": sorted(common),
                             "blocks": outcomes})
    output = {
        "historical_precondition": "Standalone payload assumption was later disproven: the decoder must retain packed bits across raw metadata reads. See 3DXML_mesh_decoding_spec.md.",
        "method": "raw bytes; complete triangle-only payload constraints",
        "configuration_fields": ["family", "parameter", "variant", "bit_order", "leading_bits"],
        "configurations_checked": checked,
        "selector_tables": SELECTOR_TABLES,
        "limitations": "Does not exclude other codecs, predictors, mixed codes or nonzero initial state.",
        "blocks": [{**{k: v for k, v in b.items() if k != "data"},
                    "payload_bytes": len(b["data"]), "individual_fit_count": len(matches),
                    "individual_fits": matches[:8]} for b, matches in zip(blocks, per_block)],
        "common_fits": fits,
    }
    args.output.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    print("Configurations:", checked, "complete blocks:", len(blocks), "common fits:", len(fits))
    print("Individual fits:", [len(matches) for matches in per_block])


if __name__ == "__main__":
    main()
