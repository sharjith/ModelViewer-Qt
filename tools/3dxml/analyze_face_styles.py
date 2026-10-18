"""Inventory ordinary style records bound one-to-one to finest face packets."""
import argparse
from collections import Counter
import json
from pathlib import Path
import runpy
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    api = runpy.run_path(str(Path(__file__).with_name("decode_representation.py")))
    rows, totals, prefixes = [], Counter(), Counter()
    with zipfile.ZipFile(args.archive) as archive:
        for member in archive.namelist():
            if not member.endswith(".3DRep") or member == "Applications.3DRep":
                continue
            data = archive.read(member)
            entries = api["compound_streams"](data)
            entry = next(e for e in entries if e["name"] == "Skeleton")
            logical = api["join_stream"](data, entry)
            def visit(leaf, mesh, groups, styles):
                assert len(styles) == len(mesh["finest_faces"])
                faces, triangles = Counter(), Counter()
                for i, (face, color) in enumerate(zip(mesh["finest_faces"], styles)):
                    raw = logical[mesh["geometry_end_offset"] + i*8:mesh["geometry_end_offset"] + (i+1)*8]
                    assert raw[5:] == bytes(color)
                    key = bytes(color).hex()
                    faces[key] += 1
                    triangles[key] += len(face["triangles"])
                    prefixes[raw[:5].hex()] += 1
                totals.update(triangles)
                rows.append({"member": member, "leaf_offset": leaf["offset"],
                             "face_packets": len(styles), "face_packets_by_stored_bgr_hex": dict(faces),
                             "triangles_by_stored_bgr_hex": dict(triangles),
                             "triangles_by_decoded_rgb_hex": {bytes.fromhex(k)[::-1].hex(): v for k,v in triangles.items()}})
            api["skeleton_graph"](logical, visit)
    report = {"scope": "finest-face style bytes; BGR ordering corroborated by owner-supplied rendered reference; no full material reconstruction",
              "style_prefix_counts": dict(prefixes), "triangles_by_stored_bgr_hex": dict(totals),
              "triangles_by_decoded_rgb_hex": {bytes.fromhex(k)[::-1].hex(): v for k,v in totals.items()},
              "meshes": rows}
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("Style prefixes:", dict(prefixes))
    print("Colors:", len(totals), "multiple-color meshes:", sum(len(r["face_packets_by_stored_bgr_hex"]) > 1 for r in rows))


if __name__ == "__main__":
    main()
