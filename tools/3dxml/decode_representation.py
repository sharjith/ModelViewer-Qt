"""Sample-derived CFV3 directory and complete Skeleton graph research decoder.

Only supplied bytes and original analysis are used. No vendor implementation.
Descriptor localization uses validated catalog names/IDs, not mesh-marker scans.
Unknown records, compressed extents and differing container versions fail closed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import struct
import zipfile
import xml.etree.ElementTree as ET

MESH = runpy.run_path(str(Path(__file__).with_name("decode_sample_meshes.py")))


def be32(data, offset):
    if offset < 0 or offset + 4 > len(data):
        raise ValueError("truncated compound integer")
    return struct.unpack_from(">I", data, offset)[0]


def descriptor(data, offset, expected_id, expected_kind):
    length = be32(data, offset)
    if length < 34 or length > 200034 or offset + length > len(data):
        raise ValueError("invalid descriptor bounds")
    record_id, kind, repeated_kind, total, reserved = struct.unpack_from(
        ">IHIII", data, offset + 4)
    if (record_id, kind, repeated_kind, reserved) != (
            expected_id, expected_kind, expected_kind, 0):
        raise ValueError("descriptor identity/type mismatch")
    count = be32(data, offset + 30)
    if length != 34 + 20 * count:
        raise ValueError("descriptor extent table length mismatch")
    extents = []
    cursor = 0
    for i in range(count):
        physical, stored, expanded, logical, flags = struct.unpack_from(
            ">IIIII", data, offset + 34 + 20 * i)
        if stored != expanded or logical != cursor or flags != 0:
            raise ValueError("unsupported compressed/noncontiguous logical extent")
        if physical < 128 or physical + stored > len(data):
            raise ValueError("physical extent out of bounds")
        extents.append({"physical": physical, "bytes": stored, "logical": logical})
        cursor += stored
    if cursor != total:
        raise ValueError("descriptor total differs from extent sizes")
    return {"id": record_id, "kind": kind, "descriptor_offset": offset,
            "bytes": total, "extents": extents}


def join_stream(data, entry):
    return b"".join(data[e["physical"]:e["physical"] + e["bytes"]]
                    for e in entry["extents"])


def catalog_records(payload, expected_id):
    if len(payload) < 12:
        raise ValueError("truncated storage catalog")
    size, identity, count = struct.unpack_from(">III", payload)
    if size != len(payload) or identity != expected_id or count > 10000:
        raise ValueError("invalid storage catalog header")
    rows = []
    cursor = 12
    for _ in range(count):
        size = be32(payload, cursor)
        if size < 16 or cursor + size > len(payload):
            raise ValueError("invalid catalog child size")
        identity, kind, name_bytes = struct.unpack_from(">IHI", payload, cursor + 4)
        if size != 14 + name_bytes or name_bytes < 2 or name_bytes % 2:
            raise ValueError("invalid catalog child name length")
        name = payload[cursor + 14:cursor + size].decode("utf-16-be")
        if not name.endswith("\0") or "\0" in name[:-1]:
            raise ValueError("invalid catalog name termination")
        rows.append((identity, kind, name[:-1]))
        cursor += size
    if cursor != len(payload):
        raise ValueError("unconsumed storage catalog data")
    return rows


def compound_streams(data):
    if not data.startswith(b"V5_CFV3\0") or len(data) > 128 * 1024 * 1024:
        raise ValueError("unsupported compound signature or size")
    # Allocation padding differs in the press files. Resolve the root through
    # the descriptor, checked against the independent header catalog endpoint.
    roots = []
    pattern = struct.pack(">IIH",54,2,1)
    start = 0
    while (at := data.find(pattern,start)) >= 0:
        start = at+1
        try:
            entry = descriptor(data,at,2,1)
            if len(entry['extents']) != 1:
                continue
            extent = entry['extents'][0]
            payload = join_stream(data,entry)
            if extent['physical']+extent['bytes']==be32(data,8) and len(payload)>=12 and be32(payload,0)==len(payload) and be32(payload,4)==2:
                roots.append(entry)
        except (ValueError,struct.error):
            pass
    if len(roots)!=1:
        raise ValueError("absent/ambiguous root catalog descriptor")
    root_offset = roots[0]['extents'][0]['physical']
    root_size = roots[0]['bytes']
    payload = data[root_offset:root_offset + root_size]
    root_id = be32(payload, 4)
    entries = []
    seen = set()

    def children(payload, identity, parent):
        for child_id, kind, name in catalog_records(payload, identity):
            if child_id in seen or kind not in (1, 2):
                raise ValueError("duplicate catalog identity or unsupported entry kind")
            seen.add(child_id)
            needle = (name + "\0").encode("utf-16-be")
            matches = []
            start = 0
            while True:
                at = data.find(needle, start)
                if at < 0:
                    break
                start = at + 1
                if at < 4 or be32(data, at - 4) != len(needle):
                    continue
                try:
                    candidate = descriptor(data, at + len(needle), child_id, kind)
                except (ValueError, struct.error):
                    continue
                matches.append(candidate)
            if len(matches) != 1:
                raise ValueError(f"catalog descriptor is absent/ambiguous: {name}")
            entry = matches[0]
            entry.update(name=name, path=parent + [name])
            entries.append(entry)
            if kind == 1 and entry["bytes"]:
                children(join_stream(data, entry), child_id, parent + [name])

    children(payload, root_id, [])
    # Some metadata-only storages have zero catalog bytes. Their children are
    # announced by ordinary parent/name headers instead. Enumerate missing IDs
    # from the compound header and validate those complete headers/descriptors.
    expected_max = struct.unpack_from(">H", data, 89)[0]
    if not 2 <= expected_max <= 10000:
        raise ValueError("compound identity range limit")
    by_id = {e["id"]: e for e in entries}
    for identity in range(1, expected_max + 1):
        if identity in seen or identity in (1, root_id):
            continue
        matches = []
        for kind in (1, 2):
            pattern = struct.pack(">IH", identity, kind)
            at = 4
            while True:
                at = data.find(pattern, at)
                if at < 0:
                    break
                q = at - 4
                at += 1
                try:
                    entry = descriptor(data, q, identity, kind)
                except (ValueError, struct.error):
                    continue
                for name_bytes in range(2, min(q - 28, 4096) + 1, 2):
                    p = q - name_bytes
                    if be32(data, p - 4) != name_bytes or be32(data, p - 24) != kind:
                        continue
                    if be32(data, p - 20) != entry["bytes"] or be32(data, p - 16) != 0:
                        continue
                    try:
                        name = data[p:q].decode("utf-16-be")
                    except UnicodeError:
                        continue
                    if not name.endswith("\0") or "\0" in name[:-1]:
                        continue
                    parent_id = be32(data, p - 28)
                    if parent_id not in (1, root_id) and parent_id not in by_id:
                        continue
                    nested = dict(entry, name=name[:-1], parent_id=parent_id)
                    matches.append(nested)
        if len(matches) != 1:
            raise ValueError("absent/ambiguous metadata-only storage descriptor")
        entry = matches[0]
        parent_id = entry["parent_id"]
        parent = by_id.get(parent_id)
        if parent and parent["kind"] != 1:
            raise ValueError("metadata parent is not a storage")
        entry["path"] = (parent["path"] if parent else []) + [entry["name"]]
        entries.append(entry)
        by_id[identity] = entry
        seen.add(identity)
    # Independent anchor: storage root's descriptor must point back to this catalog.
    pattern = struct.pack(">IIH", 54, root_id, 1)
    candidates = []
    at = 0
    while True:
        at = data.find(pattern, at)
        if at < 0:
            break
        try:
            entry = descriptor(data, at, root_id, 1)
            if entry["extents"] == [{"physical": root_offset, "bytes": root_size, "logical": 0}]:
                candidates.append(entry)
        except (ValueError, struct.error):
            pass
        at += 1
    if len(candidates) != 1:
        raise ValueError("compound root descriptor does not validate header anchor")
    if seen | {1, root_id} != set(range(1, expected_max + 1)):
        raise ValueError(f"catalog identities do not cover header identity range: {sorted(seen | {1, root_id})}, maximum {expected_max}")
    ranges = [(e["physical"], e["physical"] + e["bytes"])
              for entry in entries + candidates for e in entry["extents"] if e["bytes"]]
    ranges.sort()
    if any(b > c for (_, b), (c, _) in zip(ranges, ranges[1:])):
        raise ValueError("compound physical stream extents overlap")
    return entries


def skeleton_graph(data, visit=None):
    reader = MESH["Reader"](data, 0)
    nodes, leaves = [], []
    cache, active = {}, set()

    def attributes(flags):
        if not flags & 0x10000:
            return None
        start = reader.pos
        if data[reader.pos] == 0:
            if reader.raw(2) != bytes.fromhex("0001"):
                raise ValueError("unsupported image-free material prefix")
            parameters = reader.floats(15)
            if reader.raw(9) != bytes.fromhex("000001010500010100"):
                raise ValueError("unsupported image-free mapping prefix")
            mapping = reader.floats(19)
            if reader.raw(2) != bytes.fromhex("0100"):
                raise ValueError("unsupported image-free mapping suffix")
            return {"offset":start,"end":reader.pos,"parameters":parameters,
                    "mapping_parameters":mapping,"image_bytes":0}
        # Narrow measured material/environment-image form. Preserve parameters;
        # this only enables structural skipping, not material interpretation.
        if reader.byte() != 1:
            raise ValueError("unsupported graph attribute block")
        parameters = reader.floats(15)
        if reader.raw(3) != b"\x01\x01\x01":
            raise ValueError("unsupported attribute image prefix")
        image_id = reader.count()
        if reader.byte() != 3:
            raise ValueError("unsupported attribute image format")
        width, height, byte_count = reader.count(), reader.count(), reader.count()
        if byte_count != width * height * 4 or byte_count > 16 * 1024 * 1024:
            raise ValueError("attribute image dimensions/size mismatch")
        if reader.raw(2) != b"\x03\x01":
            raise ValueError("unsupported attribute image metadata")
        image_parameters = reader.floats(2)
        pixel_hash = hashlib.sha256(reader.raw(byte_count)).hexdigest()
        name = reader.raw(reader.count()).decode("ascii")
        if reader.raw(8) != bytes.fromhex("00 00 01 05 03 01 01 00"):
            raise ValueError("unsupported attribute mapping prefix")
        mapping = reader.floats(19)
        if reader.raw(2) != b"\x01\x00":
            raise ValueError("unsupported attribute mapping suffix")
        return {"offset": start, "end": reader.pos, "parameters": parameters,
                "image_id": image_id, "image_width": width, "image_height": height,
                "image_bytes": byte_count, "pixel_sha256": pixel_hash, "name": name,
                "image_parameters": image_parameters, "mapping_parameters": mapping}

    def node(depth, path):
        if depth > 128 or len(nodes) + len(leaves) > 100000:
            raise ValueError("graph resource limit")
        start = reader.pos
        tag = reader.byte()
        if tag in (0x62, 0xa3):
            span_start = reader.pos
            span = reader.count() if tag == 0xa3 else None
            header = reader.raw(35)
            flags = reader.count()
            extra = attributes(flags)
            count = reader.count()
            if count > 100000:
                raise ValueError("graph child count limit")
            identity = struct.unpack_from("<I", header, 31)[0]
            entry = {"offset": start, "source_id": identity,
                          "header_hex": header.hex(), "flags": flags, "children": count,
                          "path": path, "child_links": []}
            if extra:
                entry["attributes"] = extra
            nodes.append(entry)
            for i in range(count):
                mode = reader.byte()
                if mode == 0:
                    target = reader.pos
                    node(depth + 1, path + [i])
                    entry["child_links"].append({"mode": "inline", "target": target})
                elif mode == 1:
                    index = reader.count()
                    if index in cache:
                        if index in active:
                            raise ValueError("cyclic cached graph reference")
                        entry["child_links"].append({"mode": "reference", "cache": index,
                                                     "target": cache[index]})
                    else:
                        target = reader.pos
                        cache[index] = target
                        active.add(index)
                        node(depth + 1, path + [i])
                        active.remove(index)
                        entry["child_links"].append({"mode": "definition", "cache": index,
                                                     "target": target})
                else:
                    raise ValueError("unsupported graph child reference mode")
            if tag == 0xa3:
                scalar = reader.floats(1)[0]
                if scalar != 1 or reader.pos != span_start+span+1:
                    raise ValueError("extended group span/scalar mismatch")
                entry["extended_group_scalar"] = scalar
        elif tag == 0x95:
            span_start = reader.pos
            span = reader.count()
            header = reader.raw(35)
            if reader.raw(5) != bytes.fromhex("ff ff 02 00 01"):
                raise ValueError("unsupported mesh leaf layout")
            count_offset = reader.pos
            mesh = MESH["decode"](data, count_offset, reader=reader)
            face_styles = [MESH["style"](reader) for _ in mesh["finest_faces"]]
            groups = {1: [], 2: []}
            while data[reader.pos:reader.pos + 2] in (b"\x01\x01", b"\x01\x02"):
                reader.byte()
                kind = reader.byte()
                if groups[kind]:
                    raise ValueError("duplicate indexed tail group section")
                count = reader.count()
                if count > 1000000:
                    raise ValueError("tail group count limit")
                for _ in range(count):
                    if reader.byte() != 2:
                        raise ValueError("unsupported indexed tail group")
                    length = reader.count()
                    if length > 1000000:
                        raise ValueError("tail index count limit")
                    indices = [reader.bits(mesh["index_bit_width"]) for _ in range(length)]
                    if any(i >= mesh["vertex_count"] for i in indices):
                        raise ValueError("tail coordinate index out of range")
                    groups[kind].append(indices)
                style = reader.raw(8)
                if style[:5] not in (bytes.fromhex("10 24 04 ff ff"), bytes.fromhex("10 14 04 ff ff")):
                    raise ValueError("unsupported tail style")
            if reader.pos != span_start + span:
                raise ValueError(f"mesh leaf span mismatch at {start}: {reader.pos} != {span_start + span}")
            surface_offset = reader.count()
            leaf = {"offset": start, "count_offset": count_offset,
                    "end_offset": reader.pos, "path": path, "header_hex": header.hex(),
                    "surface_stream_offset": surface_offset,
                    "vertices": mesh["vertex_count"], "normal_records": mesh["normal_record_count"],
                    "faces": len(mesh["finest_faces"]),
                    "triangles": sum(len(f["triangles"]) for f in mesh["finest_faces"]),
                    "line_groups": len(groups[1]), "point_groups": len(groups[2]),
                    "remaining_bits": reader.available}
            if visit:
                visit(leaf, mesh, groups, face_styles)
            leaves.append(leaf)
        else:
            raise ValueError(f"unsupported graph record {tag:#x} at {start}")

    node(0, [])
    if reader.pos != len(data) or reader.reservoir != 0:
        raise ValueError("unconsumed Skeleton bytes or nonzero reservoir padding")
    return {"bytes_consumed": reader.pos, "remaining_zero_bits": reader.available,
            "nodes": nodes, "meshes": leaves, "cache_entries": len(cache)}


def surface_links(member, entries, graph):
    surfaces = [e for e in entries if e["name"] == "SurfacicReps" and e["kind"] == 2]
    meshes = graph["meshes"]
    if not surfaces:
        if meshes:
            raise ValueError("mesh leaves have no SurfacicReps stream")
        return {"blocks": 0, "bytes": 0}
    if len(surfaces) != 1:
        raise ValueError("ambiguous SurfacicReps stream")
    data = join_stream(member, surfaces[0])
    offsets = sorted(set(m["surface_stream_offset"] for m in meshes))
    cursor = 0
    for offset in offsets:
        if offset != cursor:
            raise ValueError("SurfacicReps block offsets do not partition stream")
        reader = MESH["Reader"](data, offset)
        size = reader.count()
        if size < reader.pos - offset or offset + size > len(data):
            raise ValueError("SurfacicReps block size out of range")
        cursor += size
    if cursor != len(data):
        raise ValueError("unreferenced SurfacicReps bytes remain")
    return {"blocks": len(offsets), "bytes": cursor}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--geometry-qa", action="store_true")
    parser.add_argument("--qa-baseline", type=Path,
                        help="Reuse existing QA only when member hash and every leaf field match")
    args = parser.parse_args()
    rows = []
    baseline = {}
    if args.qa_baseline:
        baseline = {r["member"]: r for r in json.loads(args.qa_baseline.read_text())["representations"]}
    with zipfile.ZipFile(args.archive) as archive:
        manifest = ET.fromstring(archive.read("Manifest.xml"))
        root_member = next(e.text.strip() for e in manifest.iter() if e.tag.rsplit("}", 1)[-1] == "Root")
        root = ET.fromstring(archive.read(root_member))
        formats = {e.attrib["associatedFile"].removeprefix("urn:3DXML:"): e.attrib.get("format")
                   for e in root.iter() if e.tag.rsplit("}", 1)[-1] == "ReferenceRep"}
        for member in archive.namelist():
            if not member.lower().endswith(".3drep"):
                continue
            data = archive.read(member)
            row = {"member": member, "sha256": hashlib.sha256(data).hexdigest(),
                   "xml_format": formats.get(member)}
            if row["xml_format"] == "TECHREP":
                row["status"] = "non_mesh_review_attachment"
                rows.append(row)
                print(member, "TECHREP review attachment", flush=True)
                continue
            try:
                entries = compound_streams(data)
                row["compound_entries"] = entries
                skeletons = [e for e in entries if e["name"] == "Skeleton" and e["kind"] == 2]
                if not skeletons and not any(e['name']=='SurfacicReps' for e in entries) and any(e['name']=='V4V5_FDT' and e['kind']==1 for e in entries):
                    row['status']='non_mesh_annotation_container'
                    rows.append(row)
                    print(member,'annotation container',flush=True)
                    continue
                if len(skeletons) != 1:
                    raise ValueError("no unique Skeleton stream")
                def visit(leaf, mesh, groups, styles):
                    if args.geometry_qa:
                        previous = baseline.get(member)
                        if previous and previous["sha256"] == row["sha256"]:
                            old_leaf = next((m for m in previous.get("graph", {}).get("meshes", [])
                                             if m["offset"] == leaf["offset"]), None)
                            if old_leaf and {k: v for k, v in old_leaf.items()
                                            if k not in ("geometry_checks", "candidate_normal_checks")} == leaf:
                                if "geometry_checks" in old_leaf and "candidate_normal_checks" in old_leaf:
                                    leaf["geometry_checks"] = old_leaf["geometry_checks"]
                                    leaf["candidate_normal_checks"] = old_leaf["candidate_normal_checks"]
                                    return
                        leaf["geometry_checks"] = MESH["geometry_checks"](mesh)
                        try:
                            leaf["candidate_normal_checks"] = MESH["check_normal_candidates"](
                                mesh, MESH["normal_candidates"](mesh))
                        except ValueError as error:
                            leaf["candidate_normal_checks"] = {"error": str(error)}
                row["graph"] = skeleton_graph(join_stream(data, skeletons[0]), visit)
                row["surface_links"] = surface_links(data, entries, row["graph"])
                row["status"] = "complete_skeleton_consumption"
                print(member, "OK", len(row["graph"]["meshes"]), flush=True)
            except (ValueError, struct.error, UnicodeError) as error:
                row.update(status="unresolved", reason=str(error))
                print(member, "FAIL", str(error), flush=True)
            rows.append(row)
    complete = [r for r in rows if r["status"] == "complete_skeleton_consumption"]
    meshes = [m for r in complete for m in r["graph"]["meshes"]]
    summary = {"complete_uvr_members": len(complete),
               "non_mesh_annotation_containers": sum(r["status"]=="non_mesh_annotation_container" for r in rows),
               "non_mesh_review_attachments": sum(r["status"] == "non_mesh_review_attachment" for r in rows),
               "unresolved_members": sum(r["status"] == "unresolved" for r in rows),
               "mesh_free_members": [r["member"] for r in complete if not r["graph"]["meshes"]],
               "mesh_leaves": len(meshes), "position_records": sum(m["vertices"] for m in meshes),
               "finest_triangles": sum(m["triangles"] for m in meshes),
               "linked_surface_blocks": sum(r["surface_links"]["blocks"] for r in complete)}
    if args.geometry_qa:
        summary.update(
            degenerate_triangles=sum(len(m["geometry_checks"]["degenerate_triangles"]) for m in meshes),
            constant_normal_disagreements=sum(len(m["geometry_checks"]["constant_normal_winding_mismatches"]) for m in meshes),
            candidate_normal_errors=sum("error" in m["candidate_normal_checks"] for m in meshes),
            opposed_candidate_normal_corners=sum(m["candidate_normal_checks"].get("opposed_vertex_normals", 0) for m in meshes))
    report = {"scope": "sample CFV3 catalogs and complete logical Skeleton graph",
              "source_sha256": hashlib.sha256(args.archive.read_bytes()).hexdigest(),
              "geometry_qa": args.geometry_qa, "summary": summary, "representations": rows}
    args.report.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print("Summary:", json.dumps(summary), flush=True)
    if summary["unresolved_members"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
