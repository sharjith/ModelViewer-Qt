"""Raw-sample scene graph / basis / transform evidence before C++ integration.

Produces a compact evidence report and a full occurrence fixture. Uses only
the supplied ZIP/XML/binary bytes and our own sample-derived mesh decoder.
"""

import argparse
from collections import Counter, defaultdict
import hashlib
import itertools
import json
import math
from pathlib import Path
import runpy
import statistics
import struct
import xml.etree.ElementTree as ET
import zipfile


IDENTITY = [[1.0 if r == c else 0.0 for c in range(4)] for r in range(4)]


def local(element):
    return element.tag.rsplit("}", 1)[-1]


def fields(element):
    return {local(child): (child.text or "").strip() for child in element}


def matrix(values, columns=True):
    if len(values) != 12 or not all(math.isfinite(x) for x in values):
        raise ValueError("bad relative matrix")
    return [[values[c * 3 + r] if columns else values[r * 3 + c] for c in range(3)]
            + [values[9 + r]] for r in range(3)] + [[0.0, 0.0, 0.0, 1.0]]


def multiply(a, b):
    return [[sum(a[r][k] * b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]


def transform(m, p):
    return [sum(m[r][c] * p[c] for c in range(3)) + m[r][3] for r in range(3)]


def determinant(m):
    return (m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
            - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
            + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]))


def distance(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--inventory", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--occurrences", type=Path, required=True)
    args = parser.parse_args()
    decoder = runpy.run_path(str(Path(__file__).with_name("decode_sample_meshes.py")))
    inventory = json.loads(args.inventory.read_text(encoding="utf-8"))
    decode = decoder["decode"]
    with zipfile.ZipFile(args.archive) as archive:
        manifest = ET.fromstring(archive.read("Manifest.xml"))
        root_member = next(e.text.strip() for e in manifest.iter() if local(e) == "Root")
        source = ET.fromstring(archive.read(root_member))
        product = next(e for e in source.iter() if local(e) == "ProductStructure")
        references = {e.attrib["id"]: e.attrib for e in product if local(e) == "Reference3D"}
        representations = {e.attrib["id"]: e.attrib for e in product if local(e) == "ReferenceRep"}
        ids = [e.attrib["id"] for e in product if "id" in e.attrib]
        if len(set(ids)) != len(ids):
            raise ValueError("duplicate product ID")
        children = defaultdict(list)
        rep_children = defaultdict(list)
        instances = []
        for e in product:
            if local(e) not in ("Instance3D", "InstanceRep"):
                continue
            f = fields(e)
            parent, target = f["IsAggregatedBy"], f["IsInstanceOf"]
            if parent not in references:
                raise ValueError("missing aggregation reference")
            item = {"id": e.attrib["id"], "name": e.attrib.get("name", ""),
                    "parent_reference": parent, "target_reference": target}
            if local(e) == "Instance3D":
                if target not in references:
                    raise ValueError("missing target reference")
                item["values"] = list(map(float, f["RelativeMatrix"].split()))
                item["local_matrix"] = matrix(item["values"])
                children[parent].append(item)
                instances.append(item)
            else:
                if target not in representations:
                    raise ValueError("missing representation reference")
                rep_children[parent].append(item)

        hidden_paths = []
        for prop in source.iter():
            if local(prop) != "DefaultViewProperty":
                continue
            path = tuple((e.text or "").rsplit("#", 1)[-1]
                         for e in prop.iter() if local(e) == "id")
            if any(local(e) == "GeneralAttributes" and e.attrib.get("visible") == "false"
                   for e in prop.iter()):
                hidden_paths.append(path)

        occurrences = []

        def walk(reference, path, world, ancestors, inherited_visible):
            if reference in ancestors:
                raise ValueError("reference cycle")
            ancestors = ancestors | {reference}
            for item in children[reference]:
                next_path = path + (item["id"],)
                visible = inherited_visible and next_path not in hidden_paths
                next_world = multiply(world, item["local_matrix"])
                attached = [representations[r["target_reference"]] for r in rep_children[item["target_reference"]]]
                occurrences.append({"path": list(next_path), "instance_id": item["id"],
                    "name": item["name"], "reference_id": item["target_reference"],
                    "local_matrix_rows": item["local_matrix"], "world_matrix_rows": next_world,
                    "visible": visible, "representations": attached})
                walk(item["target_reference"], next_path, next_world, ancestors, visible)

        walk(product.attrib["root"], (), IDENTITY, set(), True)
        args.occurrences.parent.mkdir(parents=True, exist_ok=True)
        args.occurrences.write_text(json.dumps({"root_reference": product.attrib["root"],
            "root_representations": [representations[r["target_reference"]]
                                     for r in rep_children[product.attrib["root"]]],
            "occurrences": occurrences}, indent=2) + "\n", encoding="utf-8")

        center_errors = {perm: [] for perm in itertools.permutations(range(3))}
        center_examples = []
        center_members = []
        calibrations = []
        bounds_by_member = {}
        for row in inventory["representations"]:
            if "count_header" not in row:
                continue
            data = archive.read(row["member"])
            if hashlib.sha256(data).hexdigest() != row["sha256"]:
                raise ValueError("member hash mismatch")
            mesh = decode(data, row["count_header"])
            points = mesh["positions_serialized"]
            bounds = [[min(p[i] for p in points), max(p[i] for p in points)] for i in range(3)]
            center = [(a + b) / 2 for a, b in bounds]
            stored_center = struct.unpack_from("<3f", data, row["count_header"] - 21)
            bounds_by_member[row["member"]] = bounds
            for perm in center_errors:
                center_errors[perm].append(distance([center[i] for i in perm], stored_center))
            center_members.append({"member": row["member"],
                "native_center_error": distance(center, stored_center)})
            if row["member"] in ("ISO_2338_5x25_Zyl_Stift.3DRep", "Distanzring_1_R.3DRep", "DIN_ISO_4762_M5x12.3DRep"):
                center_examples.append({"member": row["member"], "bounds_native_candidate": bounds,
                    "bounds_center": center, "binary_center_candidate_offset": row["count_header"] - 21,
                    "binary_center_candidate": stored_center})
            if "ISO_2338_" in row["member"]:
                calibrations.append({"member": row["member"], "bounds_spans": [b - a for a, b in bounds],
                    "maximum_radial_diameter_about_local_x": 2 * max(math.hypot(p[1], p[2]) for p in points)})
            del mesh

        pin_evidence = []
        for item in instances:
            if item["target_reference"] != "485":
                continue
            translation = item["values"][9:]
            radius = math.hypot(translation[1], translation[2])
            evidence = {"instance": item["id"], "translation": translation}
            for convention in (True, False):
                m = matrix(item["values"], convention)
                axis = [m[r][0] for r in range(3)]
                evidence["columns_inward_radial_cosine" if convention else "rows_inward_radial_cosine"] = (
                    -(axis[1] * translation[1] + axis[2] * translation[2]) / radius)
            pin_evidence.append(evidence)
        screw_evidence = []
        for item in instances:
            if item["target_reference"] == "161":
                screw_evidence.append({"instance": item["id"], "translation": item["values"][9:],
                    "column_interpretation_local_z_axis": [matrix(item["values"])[r][2] for r in range(3)],
                    "row_interpretation_local_z_axis": [matrix(item["values"], False)[r][2] for r in range(3)]})

        errors = []
        dets = []
        for item in instances:
            m = item["local_matrix"]
            dets.append(determinant(m))
            errors.append(max(abs(sum(m[k][r] * m[k][c] for k in range(3)) - (r == c))
                              for r in range(3) for c in range(3)))
        vp = next(e for e in source.iter() if local(e) == "Viewpoint")
        camera = {local(e): list(map(float, e.text.split())) for e in vp}
        camera.update({key: float(value) for key, value in vp.attrib.items() if not key.startswith("{")})
        camera["target"] = [p + s * camera["targetDistance"] for p, s in zip(camera["Position"], camera["Sight"])]
        camera["right_from_sight_cross_up"] = [
            camera["Sight"][1] * camera["Up"][2] - camera["Sight"][2] * camera["Up"][1],
            camera["Sight"][2] * camera["Up"][0] - camera["Sight"][0] * camera["Up"][2],
            camera["Sight"][0] * camera["Up"][1] - camera["Sight"][1] * camera["Up"][0]]
        counts = Counter(o["reference_id"] for o in occurrences)
        path_set = {tuple(o["path"]) for o in occurrences}
        report = {
            "method": "raw sample XML and mesh bytes; no vendor software/reference output",
            "root_member": root_member,
            "xml_counts": {"references": len(references), "instances": len(instances),
                "representation_references": len(representations),
                "representation_instances": sum(map(len, rep_children.values()))},
            "expanded_occurrences": len(occurrences),
            "visible_occurrences": sum(o["visible"] for o in occurrences),
            "maximum_path_depth": max(len(o["path"]) for o in occurrences),
            "reused_references": {k: v for k, v in counts.items() if v > 1},
            "hidden_paths": hidden_paths,
            "unresolved_hidden_paths": [p for p in hidden_paths if p not in path_set],
            "rotation_checks": {"minimum_determinant": min(dets), "maximum_determinant": max(dets),
                                "maximum_orthogonality_error": max(errors)},
            "pin_matrix_disambiguation": pin_evidence,
            "screw_matrix_disambiguation": screw_evidence,
            "coordinate_permutation_evidence": [{"permutation": perm,
                "members": len(values), "median_center_error": statistics.median(values),
                "maximum_center_error": max(values), "mean_center_error": statistics.mean(values)}
                for perm, values in center_errors.items()],
            "coordinate_center_examples": center_examples,
            "largest_native_center_residuals": sorted(center_members, key=lambda x: x["native_center_error"], reverse=True)[:12],
            "unit_calibrations": calibrations,
            "explicit_unit_xml_elements_or_attributes": [local(e) for e in source.iter()
                if "unit" in local(e).lower() or any("unit" in key.lower() for key in e.attrib)],
            "default_camera": camera,
            "nonidentity_assembly_occurrences": [{"instance": i["id"], "name": i["name"],
                "matrix": i["local_matrix"]} for i in instances if children[i["target_reference"]]
                and distance([x for r in i["local_matrix"] for x in r], [x for r in IDENTITY for x in r]) > 1e-10],
        }
        args.report.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
        print("occurrences", len(occurrences), "visible", report["visible_occurrences"], "depth", report["maximum_path_depth"])
        print("center errors", report["coordinate_permutation_evidence"])
        print("pin convention", pin_evidence)
        print("unit calibration", calibrations)


if __name__ == "__main__":
    main()
