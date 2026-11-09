"""Synthetic mathematical fixtures for later C++ porting; no application tests."""
import argparse
import json
import math
from pathlib import Path
import runpy


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    api = runpy.run_path(str(Path(__file__).with_name("analyze_scene_graph.py")))
    parent_values = [0, 1, 0, -1, 0, 0, 0, 0, 1, 10, 20, 30]
    child_values = [1, 0, 0, 0, 0, 1, 0, -1, 0, 1, 2, 3]
    parent, child = api["matrix"](parent_values), api["matrix"](child_values)
    point = [4, 5, 6]
    actual = api["transform"](api["multiply"](parent, child), point)
    reversed_result = api["transform"](api["multiply"](child, parent), point)
    assert actual == [14, 25, 38]
    assert reversed_result == [6, -34, 27]
    normal = [v / math.sqrt(61) for v in (6, 4, 3)]
    tangents = [[2, -3, 0], [2, 0, -4]]
    residuals = [sum(a*b for a, b in zip(normal, tangent)) for tangent in tangents]
    assert max(abs(v) for v in residuals) < 1e-15
    args.report.write_text(json.dumps({
        "scope": "synthetic mathematics, not additional source-file evidence",
        "parent_xml_values": parent_values, "child_xml_values": child_values,
        "local_point": point, "expected_parent_times_child_point": actual,
        "incorrect_child_times_parent_point": reversed_result,
        "nonuniform_scale": [2, 3, 4], "source_normal_direction": [1, 1, 1],
        "expected_inverse_transpose_unit_normal": normal,
        "transformed_tangents": tangents, "normal_tangent_dot_products": residuals
    }, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
