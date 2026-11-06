"""Independent streaming syntax/index/normal checks on the inspection OBJ."""
import argparse
import hashlib
import json
import math
from collections import Counter
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("obj", type=Path)
    parser.add_argument("--geometry-baseline", type=Path,
                        help="Require identical v/vn/f records to a previously exported OBJ")
    args = parser.parse_args()
    manifest = json.loads(args.obj.with_suffix(".manifest.json").read_text())
    next_objects = {entry["name"]: entry for entry in manifest["objects"]}
    vertices = normals = triangles = 0
    objects = {}
    object_bounds = {}
    bounds = [[math.inf]*3, [-math.inf]*3]
    max_normal_error = 0
    material = None
    palette = manifest.get("face_color_palette", {})
    material_counts = Counter()
    geometry_hash = hashlib.sha256()
    run_indices, run_ends = {}, {}
    name = None
    with args.obj.open("r", encoding="ascii") as source:
        for line in source:
            fields = line.split()
            if not fields or fields[0].startswith("#"):
                continue
            kind = fields[0]
            if kind in ("v", "vn", "f"):
                geometry_hash.update(line.encode("ascii"))
            if kind == "o":
                name = fields[1]
                if name in objects:
                    raise ValueError("duplicate object name")
                objects[name] = {"vertices": 0, "normals": 0, "triangles": 0}
                object_bounds[name] = [[math.inf]*3, [-math.inf]*3]
                run_indices[name] = 0
                runs = next_objects[name].get("face_materials", [])
                run_ends[name] = runs[0]["triangles"] if runs else 0
            elif kind in ("v", "vn"):
                if name is None or len(fields) != 4:
                    raise ValueError("malformed vertex/normal record")
                values = tuple(map(float, fields[1:]))
                if not all(map(math.isfinite, values)):
                    raise ValueError("nonfinite coordinate/normal")
                if kind == "v":
                    vertices += 1
                    objects[name]["vertices"] += 1
                    for i in range(3):
                        bounds[0][i] = min(bounds[0][i], values[i])
                        bounds[1][i] = max(bounds[1][i], values[i])
                        object_bounds[name][0][i] = min(object_bounds[name][0][i], values[i])
                        object_bounds[name][1][i] = max(object_bounds[name][1][i], values[i])
                else:
                    normals += 1
                    objects[name]["normals"] += 1
                    max_normal_error = max(max_normal_error, abs(sum(v*v for v in values)-1))
            elif kind == "f":
                if name is None or len(fields) != 4:
                    raise ValueError("nontriangle face")
                normal_ids = []
                expected = next_objects[name]
                runs = expected.get("face_materials")
                if runs is not None:
                    while objects[name]["triangles"] >= run_ends[name]:
                        run_indices[name] += 1
                        if run_indices[name] >= len(runs):
                            raise ValueError("face material runs exhausted")
                        run_ends[name] += runs[run_indices[name]]["triangles"]
                    if material != runs[run_indices[name]]["material"] or material not in palette:
                        raise ValueError("face packet has wrong material binding")
                material_counts[material] += 1
                for corner in fields[1:]:
                    vi, ti, ni = corner.split("/")
                    vi, ni = int(vi), int(ni)
                    if ti or not 1 <= vi <= vertices or not 1 <= ni <= normals:
                        raise ValueError("invalid vertex/normal reference")
                    if not expected["first_vertex"] <= vi < expected["first_vertex"] + expected["vertices"]:
                        raise ValueError("face leaks into another object's vertex range")
                    normal_ids.append(ni)
                if len(set(normal_ids)) != 1 or normal_ids[0] != expected["first_normal"] + objects[name]["triangles"]:
                    raise ValueError("incorrect per-triangle normal binding")
                triangles += 1
                objects[name]["triangles"] += 1
            elif kind == "mtllib":
                if not (args.obj.parent / fields[1]).is_file():
                    raise ValueError("missing companion material file")
                current = None
                material_diffuse = {}
                for mtl_line in (args.obj.parent / fields[1]).read_text().splitlines():
                    tokens = mtl_line.split()
                    if tokens and tokens[0] == "newmtl":
                        current = tokens[1]
                    elif tokens and tokens[0] == "Kd":
                        material_diffuse[current] = list(map(float, tokens[1:]))
                for key, rgb in palette.items():
                    if key not in material_diffuse or len(material_diffuse[key]) != 3 or any(abs(a - b/255) > 1e-8
                            for a,b in zip(material_diffuse[key], rgb)):
                        raise ValueError("material diffuse color differs from decoded face style")
            elif kind == "usemtl":
                material = fields[1]
            elif kind not in ("s", "g", "usemtl"):
                raise ValueError("unexpected OBJ record")
            if kind == "o":
                # Resolve once per object, outside the millions of face records.
                if name not in next_objects:
                    raise ValueError("object absent from manifest")
    for entry in manifest["objects"]:
        observed = objects.get(entry["name"])
        if observed != {"vertices": entry["vertices"], "normals": entry["triangles"], "triangles": entry["triangles"]}:
            raise ValueError("per-object counts differ from manifest")
        for actual, expected in zip(object_bounds[entry["name"]], (entry["bounds_min"], entry["bounds_max"])):
            if any(not math.isclose(a, b, rel_tol=1e-12, abs_tol=1e-9) for a, b in zip(actual, expected)):
                raise ValueError("per-object bounds differ from manifest")
    if (vertices, normals, triangles) != (manifest["vertices"], manifest["normals"], manifest["triangles"]):
        raise ValueError("OBJ totals differ from manifest")
    if max_normal_error > 1e-7:
        raise ValueError("non-unit exported normal")
    with args.obj.open("rb") as source:
        digest = hashlib.file_digest(source, "sha256").hexdigest()
    if digest != manifest["obj_sha256"]:
        raise ValueError("OBJ checksum differs from manifest")
    if args.geometry_baseline:
        baseline_hash = hashlib.sha256()
        baseline_file_hash = hashlib.sha256()
        with args.geometry_baseline.open("rb") as source:
            for line in source:
                baseline_file_hash.update(line)
                if line.startswith((b"v ", b"vn ", b"f ")):
                    baseline_hash.update(line)
        expected = json.loads(args.geometry_baseline.with_suffix(".manifest.json").read_text())["obj_sha256"]
        if baseline_file_hash.hexdigest() != expected or baseline_hash.digest() != geometry_hash.digest():
            raise ValueError("geometry differs from baseline or baseline checksum changed")
    report = {"status": "verified", "objects": len(objects), "vertices": vertices,
              "normals": normals, "triangles": triangles, "bounds": bounds,
              "maximum_normal_squared_length_error": max_normal_error, "sha256": digest,
              "geometry_record_sha256": geometry_hash.hexdigest(),
              "geometry_baseline_identical": bool(args.geometry_baseline),
              "triangle_material_counts": dict(material_counts)}
    args.obj.with_suffix(".verification.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
