// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
//
// Reads a STEP file with OCCT and writes one row per face: solid ordinal, face ordinal, surface type, area,
// bounding-box centre and diagonal, plus one "solid" row per solid with its volume. Used as an owner-side
// reference when checking the faces of an XT import (tools/parasolid/compare_face_tables.py reads the output).
//
//   parasolid_step_summary <file.step> <out.tsv>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <STEPControl_Reader.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include <cmath>
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: parasolid_step_summary <file.step> <out.tsv>\n");
        return 2;
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    STEPControl_Reader reader;
    if (reader.ReadFile(argv[1]) != IFSelect_RetDone) {
        std::printf("read failed\n");
        return 1;
    }
    std::printf("read done, transferring\n");
    reader.TransferRoots();
    const TopoDS_Shape shape = reader.OneShape();
    std::FILE* out = std::fopen(argv[2], "w");
    if (!out) return 1;
    std::fprintf(out, "kind\tsolid\tface\tsurface\tarea\tcx\tcy\tcz\tdiag\tvolume\n");
    int si = 0;
    size_t faces = 0;
    for (TopExp_Explorer es(shape, TopAbs_SOLID); es.More(); es.Next(), ++si) {
        GProp_GProps vol;
        BRepGProp::VolumeProperties(es.Current(), vol);
        Bnd_Box sb;
        BRepBndLib::Add(es.Current(), sb);
        double a0 = 0, b0 = 0, c0 = 0, a1 = 0, b1 = 0, c1 = 0;
        if (!sb.IsVoid()) sb.Get(a0, b0, c0, a1, b1, c1);
        std::fprintf(out, "solid\t%d\t-1\t-\t0\t%.6g\t%.6g\t%.6g\t%.6g\t%.8g\n", si, 0.5 * (a0 + a1), 0.5 * (b0 + b1), 0.5 * (c0 + c1),
                     std::sqrt((a1 - a0) * (a1 - a0) + (b1 - b0) * (b1 - b0) + (c1 - c0) * (c1 - c0)), vol.Mass());
        int fi = 0;
        for (TopExp_Explorer ef(es.Current(), TopAbs_FACE); ef.More(); ef.Next(), ++fi, ++faces) {
            const TopoDS_Face f = TopoDS::Face(ef.Current());
            GProp_GProps ar;
            BRepGProp::SurfaceProperties(f, ar);
            Bnd_Box fb;
            BRepBndLib::Add(f, fb);
            double x0 = 0, y0 = 0, z0 = 0, x1 = 0, y1 = 0, z1 = 0;
            if (!fb.IsVoid()) fb.Get(x0, y0, z0, x1, y1, z1);
            std::fprintf(out, "face\t%d\t%d\t%d\t%.8g\t%.6g\t%.6g\t%.6g\t%.6g\t0\n", si, fi, static_cast<int>(BRepAdaptor_Surface(f).GetType()), ar.Mass(),
                         0.5 * (x0 + x1), 0.5 * (y0 + y1), 0.5 * (z0 + z1),
                         std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0) + (z1 - z0) * (z1 - z0)));
        }
    }
    std::fclose(out);
    std::printf("solids %d, faces %zu\n", si, faces);
    return 0;
}
