#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Builds a Solid Edge part holding native wire-like sketch geometry, through Solid Edge's COM interface.

The part has one sketch on the first reference plane with two disconnected open polylines (5 points / 4 segments and 4 points / 3
segments) and six loose points. It tests whether Solid Edge writes native sketch geometry into JT as display polylines or
points (see research/jt/JT_C3_TEST_SAMPLE_PLAN.md); the earlier controls imported wires from STEP. Units are metres in the
file. The output stays under the ignored out/ folder. Export it to JT afterwards with tools/jt/solid_edge_export_jt.py.

  python -B tools/jt/solid_edge_make_wire_part.py --output out/jt-research/c3-samples/c3native.par
"""
import argparse
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
MM = 0.001

POLYLINES = [
    [(0, 0), (20, 0), (20, 20), (40, 20), (40, 40)],
    [(0, 60), (30, 60), (30, 90), (60, 90)],
]
POINTS = [(80, 0), (90, 10), (100, 20), (110, 30), (120, 40), (130, 50)]


def retry(action, tries=30, delay=1.0):
    for attempt in range(tries):
        try:
            return action()
        except Exception as error:
            if attempt == tries - 1:
                raise
            if attempt == 0:
                print("  retrying after:", error, flush=True)
            time.sleep(delay)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", required=True, type=Path, help="part file to write (.par), under out/")
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(REPO / "out"):
        raise SystemExit("Output must be under the ignored out/ folder")
    output.parent.mkdir(parents=True, exist_ok=True)

    import pythoncom
    import win32com.client as client
    from win32com.client import gencache
    pythoncom.CoInitialize()
    # Dispatch would attach to a Solid Edge the user already has open, and Quit below would then close their session.
    try:
        client.GetActiveObject("SolidEdge.Application")
    except pythoncom.com_error:
        pass
    else:
        raise SystemExit("Solid Edge is already running: close it first (this script would hide and then quit it)")
    app = retry(lambda: client.Dispatch("SolidEdge.Application"))
    app.Visible = False
    app.DisplayAlerts = False
    document = None
    try:
        raw = retry(lambda: app.Documents.Add("SolidEdge.PartDocument"))
        time.sleep(3)
        document = retry(lambda: gencache.EnsureDispatch(raw))
        print("part created", flush=True)
        plane = retry(lambda: document.RefPlanes.Item(1))
        sketch = retry(lambda: gencache.EnsureDispatch(retry(lambda: document.Sketches.AddByPlane(plane))))
        print("sketch added", flush=True)
        profile = retry(lambda: gencache.EnsureDispatch(sketch.Profile))
        lines = retry(lambda: gencache.EnsureDispatch(profile.Lines2d))
        for polyline in POLYLINES:
            for (x1, y1), (x2, y2) in zip(polyline, polyline[1:]):
                retry(lambda: lines.AddBy2Points(x1 * MM, y1 * MM, x2 * MM, y2 * MM))
        print("lines added", flush=True)
        points = retry(lambda: gencache.EnsureDispatch(profile.Points2d))
        for x, y in POINTS:
            retry(lambda: points.AddAsConstruction(x * MM, y * MM) if hasattr(points, "AddAsConstruction")
                  else points.Add(x * MM, y * MM))
        print("lines", lines.Count, "points", points.Count)
        document.SaveAs(str(output).replace("\\", "/"))
        print("saved", output)
    finally:
        try:
            if document is not None:
                document.Close(False)
        except Exception:
            pass
        try:
            app.Quit()
        except Exception:
            pass


if __name__ == "__main__":
    sys.exit(main())
