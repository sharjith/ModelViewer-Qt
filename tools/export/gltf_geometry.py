#!/usr/bin/env python3
"""Geometry totals and world-space bounds of glTF 2.0 / GLB files, read from the actual buffer data (not the declared
accessor min / max, which some exporters get wrong). Read-only; needs numpy. Run with  python -I tools/export/gltf_geometry.py

  FILE [FILE...]        print one line per file: nodes, meshes used, primitives, vertices, indices, triangles, world bounds
  --compare A B         print both and whether counts and bounds agree (world bounds to a relative tolerance of 1e-4)
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gltf_profile  # noqa: E402  (same folder)

COMPONENTS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}
DTYPE = {5120: "i1", 5121: "u1", 5122: "<i2", 5123: "<u2", 5125: "<u4", 5126: "<f4"}


def read_accessor(gltf, blob, index, base_dir):
    acc = gltf["accessors"][index]
    comps = COMPONENTS[acc["type"]]
    dt = np.dtype(DTYPE[acc["componentType"]])
    view = gltf["bufferViews"][acc["bufferView"]]
    if blob is None:  # .gltf: the buffer is an external file
        uri = gltf["buffers"][view["buffer"]]["uri"]
        from urllib.parse import unquote
        with open(os.path.join(base_dir, unquote(uri)), "rb") as f:
            data = f.read()
    else:
        data = blob
    stride = view.get("byteStride") or comps * dt.itemsize
    base = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    count = acc["count"]
    raw = np.frombuffer(data, dtype=np.uint8, count=stride * (count - 1) + comps * dt.itemsize, offset=base)
    out = np.empty((count, comps), dtype=np.float64)
    for k in range(comps):
        out[:, k] = np.ndarray((count,), dt, raw, k * dt.itemsize, (stride,))
    return out


def local_matrix(node):
    if "matrix" in node:
        return np.array(node["matrix"], dtype=np.float64).reshape(4, 4).T
    t = np.array(node.get("translation", [0, 0, 0]), dtype=np.float64)
    x, y, z, w = node.get("rotation", [0, 0, 0, 1])
    s = np.array(node.get("scale", [1, 1, 1]), dtype=np.float64)
    r = np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                  [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                  [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
    m = np.eye(4)
    m[:3, :3] = r * s
    m[:3, 3] = t
    return m


def geometry(path):
    gltf, blob = gltf_profile.load(path)
    base_dir = os.path.dirname(os.path.abspath(path))
    cache = {}  # position accessor -> (min, max, count) in local space, plus the corners for transforming
    lo = np.full(3, np.inf)
    hi = np.full(3, -np.inf)
    stats = {"nodes": len(gltf.get("nodes", [])), "meshInstances": 0, "primitives": 0, "vertices": 0, "indices": 0, "triangles": 0}
    roots = gltf["scenes"][gltf.get("scene", 0)].get("nodes", [])

    def walk(i, parent):
        node = gltf["nodes"][i]
        m = parent @ local_matrix(node)
        if "mesh" in node:
            stats["meshInstances"] += 1
            for prim in gltf["meshes"][node["mesh"]]["primitives"]:
                stats["primitives"] += 1
                acc = prim["attributes"]["POSITION"]
                if acc not in cache:
                    p = read_accessor(gltf, blob, acc, base_dir)
                    cache[acc] = (p.min(axis=0), p.max(axis=0))
                mn, mx = cache[acc]
                corners = np.array([[x, y, z, 1.0] for x in (mn[0], mx[0]) for y in (mn[1], mx[1]) for z in (mn[2], mx[2])])
                world = (m @ corners.T).T[:, :3]
                nonlocal_lo = world.min(axis=0)
                nonlocal_hi = world.max(axis=0)
                lo[:] = np.minimum(lo, nonlocal_lo)
                hi[:] = np.maximum(hi, nonlocal_hi)
                n_vert = gltf["accessors"][acc]["count"]
                n_idx = gltf["accessors"][prim["indices"]]["count"] if "indices" in prim else n_vert
                stats["vertices"] += n_vert
                stats["indices"] += n_idx
                if prim.get("mode", 4) == 4:
                    stats["triangles"] += n_idx // 3
        for c in node.get("children", []):
            walk(c, m)

    import sys as _sys
    _sys.setrecursionlimit(max(_sys.getrecursionlimit(), 20000))
    for r in roots:
        walk(r, np.eye(4))
    stats["boundsMin"] = lo.tolist()
    stats["boundsMax"] = hi.tolist()
    return stats


def describe(name, s):
    return "%s: nodes=%d meshInstances=%d primitives=%d vertices=%d indices=%d triangles=%d bounds=[%s] .. [%s]" % (
        name, s["nodes"], s["meshInstances"], s["primitives"], s["vertices"], s["indices"], s["triangles"],
        ", ".join("%.4f" % v for v in s["boundsMin"]), ", ".join("%.4f" % v for v in s["boundsMax"]))


def main(argv):
    if len(argv) == 4 and argv[1] == "--compare":
        a, b = geometry(argv[2]), geometry(argv[3])
        print(describe("A", a))
        print(describe("B", b))
        same_counts = all(a[k] == b[k] for k in ("meshInstances", "primitives", "triangles"))
        ext = np.maximum(np.abs(np.array(a["boundsMax"]) - np.array(a["boundsMin"])), 1e-9)
        same_bounds = (np.all(np.abs(np.array(a["boundsMin"]) - np.array(b["boundsMin"])) <= 1e-4 * ext)
                       and np.all(np.abs(np.array(a["boundsMax"]) - np.array(b["boundsMax"])) <= 1e-4 * ext))
        print("counts (mesh instances, primitives, triangles) equal:", same_counts, "| world bounds equal:", bool(same_bounds))
        return 0 if (same_counts and same_bounds) else 1
    if len(argv) >= 2:
        for p in argv[1:]:
            print(describe(os.path.basename(p), geometry(p)))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
