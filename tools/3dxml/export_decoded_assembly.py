"""Export all decoded mesh occurrences to OBJ for owner inspection.

World placements are baked into coordinates, then converted to OBJ Y-up for
ModelViewer's existing +90-degree X import rotation. OBJ object names and
a JSON manifest preserve occurrence/body identity. Normals derive from exact
stored triangles. No geometry reconstruction, healing or simplification.
Requires NumPy, as does the research assembly preview.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path
import runpy
import zipfile

import numpy as np


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--occurrences", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--hide-housings", action="store_true")
    parser.add_argument("--native-xyz", action="store_true",
                        help="Keep source XYZ instead of compensating ModelViewer's OBJ import rotation")
    parser.add_argument("--face-colors", action="store_true",
                        help="Use each finest-face packet's stored BGR color instead of inspection colors")
    args = parser.parse_args()
    api = runpy.run_path(str(Path(__file__).with_name("decode_representation.py")))
    scene = json.loads(args.occurrences.read_text(encoding="utf-8"))
    placements = {}
    for occurrence in scene["occurrences"]:
        if occurrence["visible"]:
            for rep in occurrence["representations"]:
                if rep["format"] == "UVR":
                    member = rep["associatedFile"].removeprefix("urn:3DXML:")
                    placements.setdefault(member, []).append(occurrence)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    material_path = args.output.with_suffix(".mtl")
    material_path.write_text(
        "# Neutral inspection materials; not decoded source materials\n"
        "newmtl decoded_mesh\nKa 0.15 0.15 0.15\nKd 0.32 0.68 0.75\nKs 0.2 0.2 0.2\nNs 24\nd 1\n\n"
        "newmtl decoded_housing\nKa 0.15 0.15 0.15\nKd 0.65 0.70 0.75\nKs 0.2 0.2 0.2\nNs 24\nd 1\n",
        encoding="ascii")
    objects = []
    palette = {}
    vertex_offset, normal_offset = 0, 0
    with args.output.open("w", encoding="ascii", newline="\n", buffering=1024*1024) as output:
        output.write("# Owner-supplied 3DXML: decoded geometry, exact stored triangle connectivity\n")
        output.write("# XML world transforms baked once; source units inferred millimetres\n")
        output.write("# Native XYZ retained\n" if args.native_xyz else
                     "# OBJ Y-up: (x,z,-y) from native XYZ; ModelViewer's +90 X import restores native orientation\n")
        output.write("# Derived flat normals; named objects flatten the source occurrence/body graph\n")
        output.write("# XML-hidden occurrences excluded; no topology repair or decimation\n")
        if args.face_colors:
            output.write("# Face-packet BGR colors decoded to RGB diffuse materials; other material properties not reproduced\n")
        output.write(f"mtllib {material_path.name}\ns off\n")
        with zipfile.ZipFile(args.archive) as archive:
            for member, occurrences in placements.items():
                housing = any(name in member for name in (
                    "Kupplungsgehaeuse_FINALPART", "Getriebedeckel_FINALPART", "Mittelgehaeuse_FINALPART"))
                if args.hide_housings and housing:
                    continue
                data = archive.read(member)
                entries = api["compound_streams"](data)
                entry = next(e for e in entries if e["name"] == "Skeleton")
                resources = {}
                def collect(leaf, mesh, groups, styles):
                    resources[leaf["offset"]] = (leaf, mesh, styles)
                graph = api["skeleton_graph"](api["join_stream"](data, entry), collect)
                api["surface_links"](data, entries, graph)
                nodes = {node["offset"]: node for node in graph["nodes"]}
                # Expand cached graph references as occurrences rather than
                # merely exporting each cached resource once.
                def mesh_instances(offset, path, ancestors):
                    if offset in ancestors:
                        raise ValueError("cyclic internal graph")
                    if offset in resources:
                        yield path, resources[offset]
                    else:
                        for i, link in enumerate(nodes[offset]["child_links"]):
                            yield from mesh_instances(link["target"], path+[i], ancestors | {offset})
                for occurrence in occurrences:
                    transform = np.asarray(occurrence["world_matrix_rows"], dtype=np.float64)
                    if not np.all(np.isfinite(transform)):
                        raise ValueError("invalid world matrix")
                    for body_path, (leaf, mesh, styles) in mesh_instances(0, [], set()):
                        name = "inst_" + "_".join(occurrence["path"]) + "__" + Path(member).stem
                        name += "__body_" + "_".join(map(str, body_path))
                        name = re.sub(r"[^A-Za-z0-9_.-]", "_", name)
                        points = np.asarray(mesh["positions_serialized"], dtype=np.float64)
                        points = points @ transform[:3, :3].T + transform[:3, 3]
                        if not args.native_xyz:
                            points = points[:, [0, 2, 1]].copy()
                            points[:, 2] *= -1
                        indices = np.asarray([tri for face in mesh["finest_faces"]
                                              for tri in face["triangles"]], dtype=np.int64)
                        a, b, c = (points[indices[:, i]] for i in range(3))
                        normals = np.cross(b-a, c-a)
                        lengths = np.linalg.norm(normals, axis=1)
                        if not np.all(np.isfinite(points)) or np.any(lengths <= 0):
                            raise ValueError("nonfinite positions or degenerate exported triangle")
                        normals /= lengths[:, None]
                        output.write(f"o {name}\ng {name}\n")
                        output.write("usemtl decoded_housing\n" if housing else "usemtl decoded_mesh\n")
                        for p in points:
                            output.write("v %.17g %.17g %.17g\n" % tuple(p))
                        for normal in normals:
                            output.write("vn %.9g %.9g %.9g\n" % tuple(normal))
                        face_materials = []
                        active_material = None
                        i = 0
                        for face, stored_bgr in zip(mesh["finest_faces"], styles):
                            if args.face_colors:
                                rgb = tuple(reversed(stored_bgr))
                                material = "face_rgb_" + bytes(rgb).hex()
                                palette[material] = list(rgb)
                                if material != active_material:
                                    output.write(f"usemtl {material}\n")
                                    active_material = material
                                face_materials.append({"material": material, "triangles": len(face["triangles"])})
                            for a, b, c in face["triangles"]:
                                normal = normal_offset + i + 1
                                output.write(f"f {vertex_offset+a+1}//{normal} {vertex_offset+b+1}//{normal} {vertex_offset+c+1}//{normal}\n")
                                i += 1
                        if i != len(indices):
                            raise ValueError("exported triangle totals differ from decoded face packets")
                        objects.append({"name": name, "member": member,
                                        "occurrence_path": occurrence["path"],
                                        "body_path": body_path, "leaf_offset": leaf["offset"],
                                        "world_matrix_rows": occurrence["world_matrix_rows"],
                                        "first_vertex": vertex_offset+1, "vertices": len(points),
                                        "first_normal": normal_offset+1, "triangles": len(indices),
                                        "bounds_min": points.min(axis=0).tolist(),
                                        "bounds_max": points.max(axis=0).tolist()})
                        if args.face_colors:
                            objects[-1]["face_materials"] = face_materials
                        vertex_offset += len(points)
                        normal_offset += len(indices)
                        print(name, len(indices), "triangles", flush=True)
    if args.face_colors:
        with material_path.open("a", encoding="ascii", newline="\n") as material_output:
            material_output.write("\n# Stored BGR face styles mapped to RGB diffuse colors\n")
            for name, rgb in sorted(palette.items()):
                diffuse = tuple(v / 255 for v in rgb)
                material_output.write(f"\nnewmtl {name}\nKa 0 0 0\n")
                material_output.write("Kd %.9g %.9g %.9g\n" % diffuse)
                material_output.write("Ks 0.2 0.2 0.2\nNs 24\nd 1\nillum 2\n")
    with args.output.open("rb") as source:
        digest = hashlib.file_digest(source, "sha256").hexdigest()
    manifest = {"source_sha256": hashlib.sha256(args.archive.read_bytes()).hexdigest(),
                "obj": args.output.name, "obj_sha256": digest, "obj_bytes": args.output.stat().st_size,
                "coordinate_basis": "native XYZ" if args.native_xyz else "OBJ Y-up: (x,z,-y) from native XYZ",
                "export_basis_rows": [[1,0,0],[0,1,0],[0,0,1]] if args.native_xyz else [[1,0,0],[0,0,1],[0,-1,0]],
                "units": "millimetres inferred from sample", "normal_policy": "derived flat triangle normals",
                "visibility": "XML default-visible occurrences; no hidden helpers",
                "housings_excluded": args.hide_housings, "vertices": vertex_offset,
                "normals": normal_offset, "triangles": normal_offset, "objects": objects,
                "color_policy": "finest-face BGR to RGB diffuse" if args.face_colors else "neutral inspection colors",
                "face_color_palette": palette}
    args.output.with_suffix(".manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print("Wrote", args.output, args.output.stat().st_size, "bytes", len(objects), "objects", normal_offset, "triangles")


if __name__ == "__main__":
    main()
