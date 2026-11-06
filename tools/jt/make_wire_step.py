#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Writes small STEP AP214 wireframe/point inputs for C3 exporter controls.

These STEP files are original inputs with known geometry. They are given to a CAD application (for example Solid Edge) to
export JT; the exported JT is the exporter's own output, the STEP file is not evidence of anything by itself. All output
stays under out/. See research/jt/JT_C3_TEST_SAMPLE_PLAN.md.
"""
import argparse
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

SAMPLES = {
    # id: (description, [("polyline", [points])...] or [("points", [points])])
    "c3p1": ("one open polyline, 5 points, 4 segments",
             [("polyline", [(0, 0, 0), (20, 0, 0), (20, 20, 0), (40, 20, 0), (40, 40, 10)])]),
    "c3p2": ("two disconnected open polylines, 4 and 3 points, 3 + 2 segments",
             [("polyline", [(0, 0, 0), (40, 0, 0), (40, 30, 0), (80, 30, 0)]),
              ("polyline", [(0, 60, 10), (30, 80, 10), (60, 60, 10)])]),
    "c3pt1": ("six points",
              [("points", [(0, 0, 0), (10, 0, 0), (10, 10, 0), (0, 10, 0), (5, 5, 10), (20, 20, 20)])]),
}


def number(value):
    return f"{float(value):.6f}".rstrip("0") + ("0" if f"{float(value):.6f}".rstrip("0").endswith(".") else "")


def step_text(name, items):
    lines = []
    next_id = [100]

    def add(text):
        entity = next_id[0]
        next_id[0] += 1
        lines.append(f"#{entity}={text};")
        return entity

    origin = add(f"CARTESIAN_POINT('',({number(0)},{number(0)},{number(0)}))")
    zdir = add(f"DIRECTION('',({number(0)},{number(0)},{number(1)}))")
    xdir = add(f"DIRECTION('',({number(1)},{number(0)},{number(0)}))")
    placement = add(f"AXIS2_PLACEMENT_3D('',#{origin},#{zdir},#{xdir})")
    length = add("(LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.))")
    angle = add("(NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.))")
    solid = add("(NAMED_UNIT(*) SI_UNIT($,.STERADIAN.) SOLID_ANGLE_UNIT())")
    uncertainty = add(f"UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(1.E-07),#{length},'distance_accuracy_value','')")
    context = add(f"(GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#{uncertainty})) "
                  f"GLOBAL_UNIT_ASSIGNED_CONTEXT((#{length},#{angle},#{solid})) REPRESENTATION_CONTEXT('ID1','3D'))")
    sets = []
    for kind, points in items:
        ids = [add(f"CARTESIAN_POINT('',({number(x)},{number(y)},{number(z)}))") for x, y, z in points]
        refs = ",".join(f"#{i}" for i in ids)
        if kind == "polyline":
            curve = add(f"POLYLINE('',({refs}))")
            sets.append(add(f"GEOMETRIC_CURVE_SET('',(#{curve}))"))
        else:
            sets.append(add(f"GEOMETRIC_SET('',({refs}))"))
    representation = add("GEOMETRICALLY_BOUNDED_WIREFRAME_SHAPE_REPRESENTATION('',(#%d,%s),#%d)"
                         % (placement, ",".join(f"#{i}" for i in sets), context))
    app = add("APPLICATION_CONTEXT('core data for automotive mechanical design processes')")
    add(f"APPLICATION_PROTOCOL_DEFINITION('international standard','automotive_design',2000,#{app})")
    product_context = add(f"PRODUCT_CONTEXT('',#{app},'mechanical')")
    product = add(f"PRODUCT('{name}','{name}','',(#{product_context}))")
    formation = add(f"PRODUCT_DEFINITION_FORMATION('','',#{product})")
    definition_context = add(f"PRODUCT_DEFINITION_CONTEXT('part definition',#{app},'design')")
    definition = add(f"PRODUCT_DEFINITION('design','',#{formation},#{definition_context})")
    shape = add(f"PRODUCT_DEFINITION_SHAPE('','',#{definition})")
    add(f"SHAPE_DEFINITION_REPRESENTATION(#{shape},#{representation})")
    header = ("ISO-10303-21;\nHEADER;\nFILE_DESCRIPTION(('ModelViewer C3 wire/point control input'),'2;1');\n"
              f"FILE_NAME('{name}.step','2026-10-31T00:00:00',(''),(''),'','','');\n"
              "FILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 0 10303 214 1 1 1 1 }'));\nENDSEC;\nDATA;\n")
    return header + "\n".join(lines) + "\nENDSEC;\nEND-ISO-10303-21;\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("samples", nargs="*", default=sorted(SAMPLES))
    args = parser.parse_args()
    folder = args.output_dir.resolve()
    if not folder.is_relative_to(REPO / "out"):
        raise SystemExit("Output must stay under the ignored out/ folder")
    folder.mkdir(parents=True, exist_ok=True)
    for sample in args.samples:
        description, items = SAMPLES[sample]
        path = folder / f"{sample}.step"
        path.write_text(step_text(sample, items), encoding="ascii", newline="\n")
        print(f"{path.name}: {description}")


if __name__ == "__main__":
    main()
