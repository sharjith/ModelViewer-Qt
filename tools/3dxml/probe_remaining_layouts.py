"""Scan all measured mesh markers, including beyond the old 4096-byte window.

Research only: marker discovery is not a compound-directory parser.
"""
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import struct
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--check-unequal-geometry", action="store_true")
    args = parser.parse_args()
    api = runpy.run_path(str(Path(__file__).with_name("decode_sample_meshes.py")))
    decode = api["decode"]
    rows = []
    with zipfile.ZipFile(args.archive) as archive:
        for member in archive.namelist():
            if not member.lower().endswith(".3drep"):
                continue
            data = archive.read(member)
            row = {"member": member, "bytes": len(data),
                   "sha256": hashlib.sha256(data).hexdigest(), "candidates": []}
            cursor = 0
            while True:
                marker = data.find(bytes.fromhex("ff ff 02 00 01"), cursor)
                if marker < 0:
                    break
                cursor = marker + 1
                offset = marker + 5
                candidate = {"count_offset": offset,
                             "header_hex": data[offset:offset + 25].hex()}
                try:
                    mesh = decode(data, offset)
                    candidate.update(status="decoded_lengths_and_references",
                                     vertices=mesh["vertex_count"],
                                     normal_records=mesh["normal_record_count"],
                                     faces=len(mesh["finest_faces"]),
                                     triangles=sum(len(f["triangles"]) for f in mesh["finest_faces"]),
                                     geometry_end=mesh["geometry_end_offset"])
                    if args.check_unequal_geometry and mesh["vertex_count"] != mesh["normal_record_count"]:
                        checks = api["geometry_checks"](mesh)
                        # Keep exact offending triangle indices for follow-up.
                        candidate["geometry_checks"] = checks
                        try:
                            candidate["candidate_normal_checks"] = api["check_normal_candidates"](
                                mesh, api["normal_candidates"](mesh))
                        except ValueError as error:
                            candidate["candidate_normal_checks"] = {"error": str(error)}
                    del mesh
                except (ValueError, struct.error, IndexError) as error:
                    candidate.update(status="unresolved", reason=str(error))
                row["candidates"].append(candidate)
            rows.append(row)
            print(member, [(c["count_offset"], c["status"], c.get("reason"))
                           for c in row["candidates"]], flush=True)
    report = {"scope": "full-member marker scan; not structural entry discovery",
              "decoded_members": sum(any(c["status"] == "decoded_lengths_and_references"
                                         for c in r["candidates"]) for r in rows),
              "representations": rows}
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
