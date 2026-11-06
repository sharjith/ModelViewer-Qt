#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Builds a small Solid Edge assembly of one part placed twice, one instance rotated, through Solid Edge's COM interface.

The Solid Edge MCP bridge only opens, imports, saves and closes documents, so this uses pywin32 directly (Windows, Solid
Edge installed). The input part is copied beside the output first and never modified; the assembly references the copy.
Output stays under the ignored out/ folder. Export it to JT afterwards with tools/jt/solid_edge_export_jt.py.

  python -B tools/jt/solid_edge_make_assembly.py --part out/jt-research/c4-samples/c4block_pmi.par \
      --output out/jt-research/c4-samples/c4asm.asm
"""
import argparse
import math
import shutil
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]


def retry(action, tries=30, delay=1.0):
    for attempt in range(tries):
        try:
            return action()
        except Exception:
            if attempt == tries - 1:
                raise
            time.sleep(delay)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--part", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path, help="assembly file to write (.asm), under out/")
    parser.add_argument("--offset-mm", type=float, default=300.0, help="distance of the second instance along X")
    parser.add_argument("--angle-deg", type=float, default=90.0, help="rotation of the second instance about Z")
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(REPO / "out"):
        raise SystemExit("Output must be under the ignored out/ folder")
    output.parent.mkdir(parents=True, exist_ok=True)
    part_copy = output.with_name(output.stem + "_part.par")
    shutil.copyfile(args.part, part_copy)

    import pythoncom
    import win32com.client as client
    # Dispatch would attach to a Solid Edge the user already has open, and Quit below would then close their session.
    try:
        pythoncom.CoInitialize()
        client.GetActiveObject("SolidEdge.Application")
    except Exception:
        pass
    else:
        raise SystemExit("Solid Edge is already running: close it first (this script would hide and then quit it)")
    app = retry(lambda: client.Dispatch("SolidEdge.Application"))
    app.Visible = False
    app.DisplayAlerts = False
    document = None
    try:
        document = retry(lambda: app.Documents.Add("SolidEdge.AssemblyDocument"))
        time.sleep(2)
        occurrences = document.Occurrences
        path = str(part_copy)
        retry(lambda: occurrences.AddWithTransform(path, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0))
        retry(lambda: occurrences.AddWithTransform(path, args.offset_mm / 1000.0, 0.0, 0.0, 0.0, 0.0,
                                                   math.radians(args.angle_deg)))
        time.sleep(1)
        print("occurrences", occurrences.Count)
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
