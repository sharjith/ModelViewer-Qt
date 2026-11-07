#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Adds the PMI kinds the earlier controls lack to a COPY of a Solid Edge part, through Solid Edge's COM interface, and exports JT.

Why: the owner's controls (c4block_pmi.par and the extended one) hold dimensions, a note, a balloon, datum targets, regions and a surface
texture symbol. Feature control frames, datum feature symbols, weld symbols, leaders, text boxes and named PMI model views have no sample,
and the values they take are many; this script fills them in with plausible ones so the importer can be studied on authentic Solid Edge
output. The input is never modified. Outputs (`<stem>_<tag>.par/.jt`, and `<stem>_<tag>_views.par/.jt` with named PMI model views) go to --folder, which must be under the ignored
out/ folder. Solid Edge must not be running (the script refuses, and quits the instance it starts; it never touches an instance it did
not start).

  python -B tools/jt/solid_edge_add_pmi.py --input out/jt-research/c4-samples/c4block_pmi.par --folder out/jt-research/c4-samples

How it works (found by experiment on Solid Edge 226.00.01.04, see research/jt/JT_PMI_STUDY.md): the PMI object of the part
(`document.PMI`) has one collection per kind; `Add` / `AddByTerminator` return nothing useful, but the `AnnotInitData` route works:
`init = collection.AnnotInitData; init.SetPlane(plane); init.SetTerminatorElement(face_or_edge, False, False, x, y, z)` and then
`collection.AddDatumFrame(init)` / `AddFeatureControlFrame(init)` / `AddWeldSymbol(init)`. The part is in metres. Every step reports success
or the error; one failing step does not stop the others.
"""
import argparse
import shutil
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

# Frame text of a feature control frame: `%XX` codes for the symbols and `%VB` for the compartment divider (what the Feature Control
# Frame Properties dialog stores when a symbol button or the Divider button is used; found by reading back a frame made by hand and by
# scanning all 676 two-letter codes and looking at the exported strokes, see research/jt/JT_PMI_STUDY.md). Typed characters are drawn
# literally in one box.
FCF_PERPENDICULARITY = "%PP%VB0.1%VBA"
FCF_FLATNESS = "%FL%VB0.05"
FCF_POSITION = "%PO%VB%DI%VB0.2%VBA%VBB%VB%MC"
# Geometric characteristic symbols, material conditions and tolerance zones, each on its own frame (name, text).
FCF_SYMBOLS = (("straightness", "%SR%VB0.05"), ("circularity", "%CI%VB0.05"), ("cylindricity", "%CY%VB0.05"),
               ("angularity", "%AN%VB0.1%VBA"), ("parallelism", "%PR%VB0.1%VBA"), ("profile of a surface", "%PS%VB0.1%VBA%VBB"),
               ("profile of a line", "%PL%VB0.1"), ("circular runout", "%CR%VB0.1%VBA"), ("total runout", "%TR%VB0.1%VBA"),
               ("concentricity", "%CO%VB%DI0.1%VBA"), ("symmetry", "%SY%VB0.1%VBA"),
               ("least material", "%PO%VB%DI%VB0.2%LC%VBA%LC"), ("regardless of feature size", "%PO%VB%DI%VB0.2%SC%VBA%SC"),
               ("projected zone", "%PP%VB0.1%PZ%VBA"), ("statistical tolerance", "%PP%VB0.1%ST%VBA"))


def edge_running():
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq Edge.exe"], capture_output=True, text=True).stdout
    return "Edge.exe" in out


def retry(action, tries=40, delay=1.5):
    for attempt in range(tries):
        try:
            return action()
        except Exception:
            if attempt == tries - 1:
                raise
            time.sleep(delay)


def step(label, action):
    try:
        value = retry(action, tries=4, delay=1.0)
        print("  ok   ", label, flush=True)
        return value
    except Exception as error:
        print("  FAILED", label, "-", ascii(str(error)[:160]), flush=True)
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--input", required=True, type=Path, help="the .par to start from (not modified)")
    parser.add_argument("--folder", required=True, type=Path, help="output folder under out/")
    parser.add_argument("--tag", default="more", help="output name suffix (default more): <stem>_<tag>.par/.jt and <stem>_<tag>_views.par/.jt")
    args = parser.parse_args()
    folder = args.folder.resolve()
    if not folder.is_relative_to(REPO / "out"):
        raise SystemExit("Folder must be under the ignored out/ folder")
    folder.mkdir(parents=True, exist_ok=True)
    if edge_running():
        raise SystemExit("Solid Edge (Edge.exe) is running: close it first; this script does not touch a running instance")
    out_par = folder / (args.input.stem + "_" + args.tag + ".par")
    out_jt = out_par.with_suffix(".jt")
    out_views_par = folder / (args.input.stem + "_" + args.tag + "_views.par")
    out_views_jt = out_views_par.with_suffix(".jt")
    shutil.copyfile(args.input, out_par)

    import pythoncom
    import win32com.client

    pythoncom.CoInitialize()
    app = None
    try:
        app = win32com.client.Dispatch("SolidEdge.Application")
        retry(lambda: setattr(app, "Visible", False))
        retry(lambda: setattr(app, "DisplayAlerts", False))
        doc = retry(lambda: app.Documents.Open(str(out_par)))
        pmi = retry(lambda: doc.PMI)
        body = retry(lambda: doc.Models.Item(1).Body)
        faces = body.Faces(1)
        plane = pmi.Dimensions.Item(1).PMIPlane          # the plane of the first dimension (XZ in the controls)
        step("set the active PMI plane", lambda: setattr(pmi, "ActivePlane", plane))
        print("faces", faces.Count, "ref planes", doc.RefPlanes.Count)

        def centre(face):
            points = []
            edges = face.Edges
            for i in range(1, edges.Count + 1):
                e = edges.Item(i)
                points.append(tuple(e.StartVertex.GetPointData()))
                points.append(tuple(e.EndVertex.GetPointData()))
            return tuple(sum(c) / len(points) for c in zip(*points))

        def add(collection, adder, terminator, point, use_plane=None):
            init = collection.AnnotInitData
            init.SetPlane(use_plane if use_plane is not None else plane)
            init.SetTerminatorElement(terminator, False, False, point[0], point[1], point[2])
            return getattr(collection, adder)(init)

        # Faces of the control part (100 mm stepped block): 6 = the 96 x 96 bottom (z = 0), 7 = the opposite-sized side (x = 0).
        f_bottom, f_side = faces.Item(6), faces.Item(7)
        c_bottom, c_side = centre(f_bottom), centre(f_side)

        # Datum feature symbols A and B
        for label, face, point in (("A", f_bottom, c_bottom), ("B", f_side, c_side)):
            symbol = step("datum feature symbol " + label, lambda: add(pmi.DatumFrames, "AddDatumFrame", face, point))
            if symbol is not None:
                step("  label " + label, lambda: setattr(symbol, "Datum", label))

        # Feature control frames: the three of the first version, then one per remaining symbol on a grid on the side face (x = 0)
        frames = [("perpendicularity", FCF_PERPENDICULARITY, f_side, (c_side[0], c_side[1] + 0.02, c_side[2] + 0.02)),
                  ("flatness", FCF_FLATNESS, f_bottom, (c_bottom[0] + 0.02, c_bottom[1], c_bottom[2])),
                  ("position", FCF_POSITION, f_side, (c_side[0], c_side[1] - 0.02, c_side[2] - 0.02))]
        for k, (name, text) in enumerate(FCF_SYMBOLS):
            frames.append((name, text, f_side, (c_side[0], -0.04 + 0.025 * (k % 4), 0.012 + 0.012 * (k // 4))))
        for name, text, face, point in frames:
            frame = step("feature control frame " + name, lambda: add(pmi.FeatureControlFrames, "AddFeatureControlFrame", face, point))
            if frame is not None:
                step("  text " + name, lambda: setattr(frame, "PrimaryFrame", text))

        # Weld symbols: fillet (top), V groove all around, bottom fillet with a tail note
        edges = f_bottom.Edges
        welds = (("fillet 5 top", 1, "5", None, False, None),
                 ("V groove 6 all around", 5, "6", None, True, None),
                 ("fillet 4 bottom, tail TYP", None, None, 22, False, "TYP"))
        for k, (name, top_type, top_note, bottom_type, all_around, tail) in enumerate(welds):
            edge = edges.Item(1 + k)
            ec = tuple(sum(c) / 2.0 for c in zip(tuple(edge.StartVertex.GetPointData()), tuple(edge.EndVertex.GetPointData())))
            weld = step("weld symbol " + name, lambda: add(pmi.WeldSymbols, "AddWeldSymbol", edge, ec))
            if weld is None:
                continue
            # The weld symbols are in the PMI collection and in the exported JT, but Solid Edge itself did not draw them in the first
            # version; HidePMI is the one visibility switch the object has.
            step("  show PMI", lambda: setattr(weld, "HidePMI", False))
            if top_type is not None:
                step("  top type", lambda: setattr(weld, "TopType", top_type))
                step("  top note", lambda: setattr(weld, "TopNote1", top_note))
            if bottom_type is not None:
                step("  bottom type", lambda: setattr(weld, "BottomType", bottom_type))
                step("  bottom note", lambda: setattr(weld, "BottomNote1", "4"))
            if all_around:
                step("  all around", lambda: setattr(weld, "WeldAllAround", True))
            if tail:
                step("  tail note", lambda: setattr(weld, "TailNote", tail))

        # A free leader and a PMI text box
        step("leader", lambda: pmi.Leaders.Add(c_side[0], c_side[1] + 0.03, c_side[2] + 0.03, c_side[0], c_side[1] + 0.045, c_side[2] + 0.045))
        box = step("PMI text box", lambda: pmi.TextBoxes.AddPMITextBox(plane, c_side[0], c_side[1] + 0.03, c_side[2] - 0.03))
        if box is not None:
            step("  text", lambda: setattr(box, "Text", "Break all sharp edges"))

        print("counts: dimensions", pmi.Dimensions.Count, "datum frames", pmi.DatumFrames.Count, "fcf", pmi.FeatureControlFrames.Count,
              "weld", pmi.WeldSymbols.Count, "leaders", pmi.Leaders.Count, "text boxes", pmi.TextBoxes.Count)
        retry(lambda: doc.Save())
        retry(lambda: doc.SaveAs(str(out_jt)))
        print("wrote", out_par.name, "and", out_jt.name, out_jt.stat().st_size, "bytes")

        # Named PMI model views go into a second pair of files: a JT with model views has a different PMI layout (and the importer
        # rejects it with a diagnostic), so they must not hide the other kinds.
        step("PMI model view 'Front PMI'", lambda: pmi.PMIModelViews.Add("Front PMI", 1))
        step("PMI model view by orientation (top)", lambda: pmi.PMIModelViews.AddByStandardViewOrientation(1, 1, 0))
        print("model views", pmi.PMIModelViews.Count)
        retry(lambda: doc.SaveAs(str(out_views_par)))
        retry(lambda: doc.SaveAs(str(out_views_jt)))
        retry(lambda: doc.Close(False))
        print("wrote", out_views_par.name, "and", out_views_jt.name, out_views_jt.stat().st_size, "bytes")
    finally:
        try:
            if app is not None:
                retry(lambda: app.Quit())
        except Exception as error:
            print("quit:", error)
        time.sleep(3)
        print("Edge.exe still running:", edge_running())


if __name__ == "__main__":
    main()
