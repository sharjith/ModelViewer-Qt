"""Sample-derived mesh decoder prototype for the pin and ring.

Keeps the packed-bit reservoir across ordinary byte/float/count reads. This
is the critical observed serialization rule. Supports measured position/normal-count
layouts, with three full mesh fixtures and an optional wider audit. Not a general
CFV3/3DXML importer. OBJ normals are derived from decoded triangles; original
normal words/tags are preserved separately and not claimed decoded exactly.
"""

import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import struct
import zipfile


TARGETS = {
    "ISO_2338_5x25_Zyl_Stift.3DRep": 0x30D,
    "Distanzring_1_R.3DRep": 0x31A,
    "DIN_ISO_4762_M5x12.3DRep": 0x3D6,
}


class Reader:
    def __init__(self, data, offset):
        self.data = data
        self.pos = offset
        self.reservoir = 0
        self.available = 0

    def raw(self, size):
        if size < 0 or self.pos + size > len(self.data):
            raise ValueError("truncated raw field")
        value = self.data[self.pos:self.pos + size]
        self.pos += size
        return value

    def byte(self):
        return self.raw(1)[0]

    def count(self):
        value = self.byte()
        return struct.unpack("<I", self.raw(4))[0] if value == 255 else value

    def floats(self, count):
        return struct.unpack("<%df" % count, self.raw(count * 4))

    def bits(self, width):
        # Raw reads deliberately do NOT clear this state.
        while self.available < width:
            word = struct.unpack("<I", self.raw(4))[0]
            self.reservoir |= word << self.available
            self.available += 32
        value = self.reservoir & ((1 << width) - 1)
        self.reservoir >>= width
        self.available -= width
        return value


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def primitive(reader, width, vertices):
    start = reader.pos
    if reader.byte() != 1:
        raise ValueError(f"unexpected primitive envelope at {start:#x}")
    flags = reader.byte()
    if not 0x41 <= flags <= 0x4F:
        raise ValueError(f"unsupported primitive flags {flags:#x}")
    counts = [reader.count() if flags & bit else 0 for bit in (1, 2, 4)]
    index_count = reader.count()
    if index_count > 10_000_000 or sum(counts) > index_count:
        raise ValueError("implausible primitive counts")
    constant = reader.floats(3) if flags & 8 else None
    packed_start = reader.pos
    carry_before = reader.available
    strips = [reader.bits(width) for _ in range(counts[1])]
    fans = [reader.bits(width) for _ in range(counts[2])]
    if any(length < 3 for length in strips + fans):
        raise ValueError("short strip/fan")
    if 3 * counts[0] + sum(strips) + sum(fans) != index_count:
        raise ValueError("primitive lengths do not reconcile")
    indices = [reader.bits(width) for _ in range(index_count)]
    if any(index >= vertices for index in indices):
        raise ValueError("out-of-range vertex reference")
    triangles = [tuple(indices[i:i + 3]) for i in range(0, 3 * counts[0], 3)]
    cursor = 3 * counts[0]
    for length in strips:
        part = indices[cursor:cursor + length]
        cursor += length
        for i in range(length - 2):
            triangles.append((part[i], part[i + 1], part[i + 2]) if not i & 1
                             else (part[i + 1], part[i], part[i + 2]))
    for length in fans:
        part = indices[cursor:cursor + length]
        cursor += length
        triangles.extend((part[0], part[i], part[i + 1]) for i in range(1, length - 1))
    if cursor != index_count:
        raise ValueError("unused references")
    return {
        "header_offset": start, "flags": flags,
        "triangle_list_count": counts[0], "strip_lengths": strips, "fan_lengths": fans,
        "index_count": index_count, "indices": indices, "triangles": triangles,
        "constant_normal_serialized": constant,
        "packed_words_start": packed_start, "packed_words_end": reader.pos,
        "bits_available_before": carry_before, "bits_available_after": reader.available,
    }


def style(reader):
    # Only this exact sample style form is understood. Preserve RGB without
    # making material support part of the geometry milestone.
    if reader.raw(5) not in (bytes.fromhex("30 04 04 ff ff"), bytes.fromhex("b0 04 04 ff 7f")):
        raise ValueError(f"unsupported style at {reader.pos - 5:#x}")
    return list(reader.raw(3))


def decode(data, offset, reader=None):
    # A logical Skeleton stream may contain many leaves sharing one reservoir.
    if reader is None:
        reader = Reader(data, offset)
    elif reader.data is not data or reader.pos != offset:
        raise ValueError("mesh reader does not match logical stream cursor")
    count = reader.count()
    second = reader.count()
    if not 10 < count <= 1_000_000 or not 0 < second <= count:
        raise ValueError("unsupported vertex counts")
    prefix = reader.raw(3)
    if prefix != b"\0\0\0":
        raise ValueError("unsupported three-byte prefix")
    width = (count - 1).bit_length()
    raw_positions = reader.bits(1)
    if raw_positions:
        floats = reader.floats(count * 3)
        points = [tuple(floats[i:i + 3]) for i in range(0, len(floats), 3)]
    else:
        packed = reader.raw((count + 3) // 4)
        symbols = [(byte >> shift) & 3 for byte in packed for shift in (0, 2, 4, 6)][:count]
        float_count = reader.count()
        if float_count != sum((symbol + 3) % 4 for symbol in symbols):
            raise ValueError("coordinate float count mismatch")
        floats = reader.floats(float_count)
        previous = [0.0, 0.0, 0.0]
        points = []
        cursor = 0
        for symbol in symbols:
            length = (symbol + 3) % 4
            if length:
                previous[3 - length:] = floats[cursor:cursor + length]
            cursor += length
            points.append(tuple(previous))
    if len(points) != count or any(not math.isfinite(component) for point in points for component in point):
        raise ValueError("invalid decoded positions")
    normal_count = reader.count()
    normal_words = struct.unpack("<%dh" % normal_count, reader.raw(normal_count * 2))
    # The second count is the normal-record count, not a duplicate vertex count.
    # This differs from position count in ten measured first-body layouts.
    packed = reader.raw((second + 1) // 2)
    normal_tags = [(byte >> shift) & 15 for byte in packed for shift in (0, 4)][:second]
    if any(tag > 11 for tag in normal_tags) or normal_count != 2 * sum(tag < 6 for tag in normal_tags):
        raise ValueError("normal attribute counts mismatch")
    initial_scalar = reader.floats(1)[0]
    coarse_count = reader.count()
    coarse = []
    following_scalars = []
    for _ in range(coarse_count):
        coarse.append(primitive(reader, width, count))
        following_scalars.append(reader.floats(1)[0])
        style(reader)
    container_offset = reader.pos
    container = reader.raw(3)
    if container not in (bytes.fromhex("03 02 0a"), bytes.fromhex("02 02 0a")):
        raise ValueError(f"unsupported finest-face container at {container_offset:#x}: {container.hex()}")
    face_count = reader.count()
    faces = [primitive(reader, width, count) for _ in range(face_count)]
    return {
        "vertex_count": count, "normal_record_count": second, "index_bit_width": width,
        "positions_serialized": points,
        "normal_words_signed_le": normal_words, "normal_tags": normal_tags,
        "initial_scalar": initial_scalar, "following_scalars": following_scalars,
        "coarse_blocks": coarse, "finest_faces": faces,
        "geometry_end_offset": reader.pos, "bits_remaining": reader.available,
        "reservoir_remaining": reader.reservoir,
    }


def audit_inventory(archive_path, inventory_path, output_path):
    inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
    results = []
    with zipfile.ZipFile(archive_path) as archive:
        for row in inventory["representations"]:
            if "count_header" not in row:
                continue
            data = archive.read(row["member"])
            if hashlib.sha256(data).hexdigest() != row["sha256"]:
                raise ValueError("inventory/sample hash mismatch")
            result = {"member": row["member"]}
            try:
                mesh = decode(data, row["count_header"])
                result.update(status="decoded_lengths_and_references",
                    vertices=mesh["vertex_count"], faces=len(mesh["finest_faces"]),
                    triangles=sum(len(face["triangles"]) for face in mesh["finest_faces"]),
                    coarse_blocks=len(mesh["coarse_blocks"]),
                    geometry_end_offset=mesh["geometry_end_offset"])
                del mesh
            except (ValueError, struct.error) as error:
                result.update(status="unsupported_or_unresolved", reason=str(error))
            results.append(result)
    report = {"scope": "recognized equal-count layouts; range/length checks, not full geometry QA",
              "decoded": sum(r["status"] == "decoded_lengths_and_references" for r in results),
              "attempted": len(results), "representations": results}
    output_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("Inventory audit:", report["decoded"], "of", report["attempted"])


def geometry_checks(mesh):
    points = mesh["positions_serialized"]
    triangles = [tri for face in mesh["finest_faces"] for tri in face["triangles"]]
    unique = {point: i for i, point in enumerate(dict.fromkeys(points))}
    used = set()
    edges = Counter()
    directed = Counter()
    degenerate = []
    volume = 0.0
    constant_mismatches = []
    triangle_index = 0
    for face in mesh["finest_faces"]:
        for tri in face["triangles"]:
            a, b, c = [points[index] for index in tri]
            normal = cross(sub(b, a), sub(c, a))
            if dot(normal, normal) < 1e-16:
                degenerate.append(triangle_index)
            if face["constant_normal_serialized"] is not None and dot(normal, face["constant_normal_serialized"]) <= 0:
                constant_mismatches.append(triangle_index)
            volume += dot(a, cross(b, c)) / 6.0
            welded = [unique[points[index]] for index in tri]
            used.update(welded)
            for x, y in zip(welded, welded[1:] + welded[:1]):
                edges[tuple(sorted((x, y)))] += 1
                directed[(x, y)] += 1
            triangle_index += 1
    orientation_errors = sum(directed[(a, b)] != directed[(b, a)] for a, b in edges)
    return {
        "vertices_in_pool": len(points), "unique_positions": len(unique),
        "used_unique_positions": len(used), "triangles": len(triangles),
        "welded_edges": len(edges), "edge_incidence_histogram": dict(Counter(edges.values())),
        "edge_orientation_mismatches": orientation_errors,
        "euler_characteristic": len(used) - len(edges) + len(triangles),
        "signed_volume_serialized": volume, "degenerate_triangles": degenerate,
        "constant_normal_winding_mismatches": constant_mismatches,
    }


def normal_candidates(mesh):
    """Candidate dominant-axis SNORM16 reconstruction, in serialized order."""
    words = mesh["normal_words_signed_le"]
    cursor = 0
    normals = []
    for tag in mesh["normal_tags"]:
        normal = [0.0, 0.0, 0.0]
        if tag < 6:
            axis = tag // 2
            others = [i for i in range(3) if i != axis]
            u, v = words[cursor:cursor + 2]
            cursor += 2
            normal[others[0]], normal[others[1]] = u / 32767, v / 32767
            remainder = 1 - dot(normal, normal)
            if remainder < -0.0001:
                raise ValueError("normal candidate outside unit sphere")
            normal[axis] = (1 if not tag & 1 else -1) * math.sqrt(max(0, remainder))
        else:
            normal[(tag - 6) // 2] = 1 if not tag & 1 else -1
        normals.append(tuple(normal))
    if cursor != len(words):
        raise ValueError("unconsumed normal words")
    return normals


def check_normal_candidates(mesh, normals):
    dots = []
    used = set()
    overridden = set()
    for face in mesh["finest_faces"]:
        if face["constant_normal_serialized"] is not None:
            overridden.update(face["indices"])
            continue
        used.update(face["indices"])
        for tri in face["triangles"]:
            a, b, c = [mesh["positions_serialized"][i] for i in tri]
            normal = cross(sub(b, a), sub(c, a))
            length = math.sqrt(dot(normal, normal))
            if length:
                if any(i >= len(normals) for i in tri):
                    raise ValueError("pool-normal reference has no measured normal record")
                dots.extend(dot(normal, normals[i]) / length for i in tri)
    return {
        "status": "candidate: exact source scaling still unconfirmed",
        "vertices_using_pool_normals": len(used),
        "vertices_on_constant_normal_faces": len(overridden),
        "minimum_dot_with_triangle_normal": min(dots),
        "opposed_vertex_normals": sum(value <= 0 for value in dots),
        "max_unit_length_error": max(abs(dot(n, n) - 1) for n in normals),
    }


def write_obj(path, mesh, smooth_candidates=None):
    points = mesh["positions_serialized"]
    triangles = [tri for face in mesh["finest_faces"] for tri in face["triangles"]]
    lines = ["# Research: decoded references; " + ("candidate source smooth normals" if smooth_candidates else "derived flat normals"),
             "# Native source XYZ retained; no display-axis permutation"]
    lines.extend("v %.9g %.9g %.9g" % tuple(p) for p in points)
    normal_indices = []
    for tri in triangles:
        a, b, c = [points[index] for index in tri]
        normal = cross(sub(b, a), sub(c, a))
        length = math.sqrt(dot(normal, normal))
        normal = tuple(component / length for component in normal) if length else (0, 0, 0)
        if smooth_candidates is None:
            lines.append("vn %.9g %.9g %.9g" % tuple(normal))
    if smooth_candidates is not None:
        for face in mesh["finest_faces"]:
            for tri in face["triangles"]:
                per_triangle = []
                for index in tri:
                    normal = face["constant_normal_serialized"] or smooth_candidates[index]
                    lines.append("vn %.9g %.9g %.9g" % tuple(normal))
                    per_triangle.append(len(normal_indices) * 3 + len(per_triangle) + 1)
                normal_indices.append(per_triangle)
    cursor = 0
    for i, face in enumerate(mesh["finest_faces"]):
        lines.append(f"g face_{i + 1}")
        for tri in face["triangles"]:
            cursor += 1
            ns = [cursor] * 3 if smooth_candidates is None else normal_indices[cursor - 1]
            lines.append("f " + " ".join(f"{index + 1}//{n}" for index, n in zip(tri, ns)))
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--audit-inventory", type=Path)
    args = parser.parse_args()
    args.output_directory.mkdir(parents=True, exist_ok=True)
    report = {"status": "sample prototype; source per-vertex normals still unresolved", "representations": []}
    with zipfile.ZipFile(args.archive) as archive:
        for member, offset in TARGETS.items():
            data = archive.read(member)
            mesh = decode(data, offset)
            checks = geometry_checks(mesh)
            normals = normal_candidates(mesh)
            checks["candidate_normals"] = check_normal_candidates(mesh, normals)
            stem = Path(member).stem
            write_obj(args.output_directory / (stem + ".obj"), mesh)
            write_obj(args.output_directory / (stem + ".smooth-candidate.obj"), mesh, normals)
            (args.output_directory / (stem + ".mesh.json")).write_text(
                json.dumps(mesh, indent=2, allow_nan=False) + "\n", encoding="utf-8")
            report["representations"].append({"member": member,
                "sha256": hashlib.sha256(data).hexdigest(), "checks": checks,
                "geometry_end_offset": mesh["geometry_end_offset"], "remaining_bits": mesh["bits_remaining"]})
            print(member, json.dumps(checks))
    (args.output_directory / "verification.json").write_text(
        json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    if args.audit_inventory:
        audit_inventory(args.archive, args.audit_inventory, args.output_directory / "inventory_audit.json")


if __name__ == "__main__":
    main()
