"""Compare candidate normal words with analytic normals on selected sample parts.

Analytic normals inferred from positions are an independent geometric check,
not access to unquantized exporter normals. Do not call this exact codec proof.
"""
import argparse
import json
import math
from pathlib import Path
import runpy
import statistics
import struct
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    api = runpy.run_path(str(Path(__file__).with_name("decode_representation.py")))
    codec = api["MESH"]
    names = ["ISO_2338_5x25_Zyl_Stift.3DRep", "Distanzring_1_R.3DRep",
             "Kugel_3_4.3DRep", "Kugel_3_4_50.3DRep"]
    rows = []
    with zipfile.ZipFile(args.archive) as archive:
        for name in names:
            data = archive.read(name)
            stream = next(e for e in api["compound_streams"](data) if e["name"] == "Skeleton")
            def visit(leaf, mesh, groups, styles):
                candidates = codec["normal_candidates"](mesh)
                used = {i for f in mesh["finest_faces"] if f["constant_normal_serialized"] is None
                        for i in f["indices"]}
                center = struct.unpack_from("<3f", bytes.fromhex(leaf["header_hex"]), 19)
                coefficients, angles = [], []
                cursor = 0
                for i, tag in enumerate(mesh["normal_tags"]):
                    words = mesh["normal_words_signed_le"][cursor:cursor + 2] if tag < 6 else ()
                    cursor += len(words)
                    if i not in used:
                        continue
                    point = mesh["positions_serialized"][i]
                    if name.startswith("Kugel"):
                        expected = [point[j] - center[j] for j in range(3)]
                        length = math.sqrt(sum(v*v for v in expected))
                        expected = [v/length for v in expected]
                    else:
                        # Pin has chamfers: only its axial-zero pool normals are
                        # candidates for this independent cylindrical comparison.
                        if abs(candidates[i][0]) > 1e-12:
                            continue
                        radius = math.hypot(point[1], point[2])
                        sign = -1 if name.startswith("Distanz") and radius < 30 else 1
                        expected = [0, sign * point[1]/radius, sign * point[2]/radius]
                    cosine = sum(a*b for a, b in zip(expected, candidates[i]))
                    angles.append(math.degrees(math.acos(max(-1, min(1, cosine)))))
                    if tag < 6:
                        axes = [j for j in range(3) if j != tag // 2]
                        coefficients.extend(zip(words, (expected[j] for j in axes)))
                fits = []
                for scale in (32767, 32768):
                    residuals = [word - expected * scale for word, expected in coefficients]
                    absolute = sorted(abs(v) for v in residuals)
                    fits.append({"scale": scale, "coefficient_comparisons": len(absolute),
                                 "max_error_in_integer_units": max(absolute),
                                 "p95_error_in_integer_units": absolute[int(.95 * len(absolute))],
                                 "outside_nearest_integer_tolerance_0_51": sum(v > .51 for v in absolute),
                                 "mean_error_toward_positive_magnitude": statistics.mean(
                                     e * (1 if value >= 0 else -1)
                                     for e, (_, value) in zip(residuals, coefficients))})
                rows.append({"member": name, "selected_vertices": len(angles),
                             "selection": "finest nonconstant-face vertices; pin chamfers excluded",
                             "analytic_assumption": "sphere radial" if name.startswith("Kugel") else "cylinder radial",
                             "candidate_scale_32767_max_angle_degrees": max(angles),
                             "coefficient_fits": fits})
            api["skeleton_graph"](api["join_stream"](data, stream), visit)
    args.report.write_text(json.dumps({"status": "geometric corroboration; exact exporter quantizer unproven",
                                      "comparisons": rows}, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
