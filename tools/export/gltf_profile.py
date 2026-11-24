#!/usr/bin/env python3
"""Structural profile and diff of glTF 2.0 / GLB files, for the own-exporter work (research/export/EXPORTERS_PLAN.md).

Read-only analysis; stdlib only. Run with  python -I tools/export/gltf_profile.py ...

  profile FILE [FILE...]      print a profile of each file (counts, extensions, material extension keys, bounds)
  diff OLD NEW                compare two files and list what differs (counts, extensions, materials, names, bounds)
  summary DIR OUT.md          profile every .glb / .gltf under DIR (recursively) into one Markdown table

Order of arrays and generated names are not compared; counts, extension sets, per-material extension keys, named
nodes / materials / animations / cameras and the position bounds are. Output of the tool goes under out/ (never into Git).
"""
import json
import math
import os
import struct
import sys


def load(path):
    """Returns (json_dict, bin_bytes or None) for a .gltf or .glb file."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] == b"glTF":
        _magic, _version, total = struct.unpack_from("<4sII", data, 0)
        offset = 12
        gltf, blob = None, None
        while offset + 8 <= min(total, len(data)):
            length, ctype = struct.unpack_from("<I4s", data, offset)
            chunk = data[offset + 8: offset + 8 + length]
            if ctype == b"JSON":
                gltf = json.loads(chunk.decode("utf-8"))
            elif ctype == b"BIN\x00":
                blob = chunk
            offset += 8 + ((length + 3) & ~3)
        if gltf is None:
            raise ValueError("no JSON chunk in " + path)
        return gltf, blob
    return json.loads(data.decode("utf-8")), None


def position_bounds(gltf):
    """Union of the POSITION accessors' min / max (local space, not transformed). None when absent."""
    lo = [math.inf] * 3
    hi = [-math.inf] * 3
    seen = False
    for mesh in gltf.get("meshes", []):
        for prim in mesh.get("primitives", []):
            idx = prim.get("attributes", {}).get("POSITION")
            if idx is None:
                continue
            acc = gltf["accessors"][idx]
            if "min" in acc and "max" in acc:
                seen = True
                for i in range(3):
                    lo[i] = min(lo[i], acc["min"][i])
                    hi[i] = max(hi[i], acc["max"][i])
    return (lo, hi) if seen else None


def profile(path):
    gltf, blob = load(path)
    materials = gltf.get("materials", [])
    mat_ext = {}
    for m in materials:
        for key in (m.get("extensions") or {}):
            mat_ext[key] = mat_ext.get(key, 0) + 1
    attrs = {}
    modes = {}
    for mesh in gltf.get("meshes", []):
        for prim in mesh.get("primitives", []):
            for a in prim.get("attributes", {}):
                attrs[a] = attrs.get(a, 0) + 1
            modes[prim.get("mode", 4)] = modes.get(prim.get("mode", 4), 0) + 1
    node_ext = {}
    for n in gltf.get("nodes", []):
        for key in (n.get("extensions") or {}):
            node_ext[key] = node_ext.get(key, 0) + 1
    return {
        "file": os.path.basename(path),
        "bytes": os.path.getsize(path),
        "bin_bytes": len(blob) if blob is not None else None,
        "scenes": len(gltf.get("scenes", [])),
        "nodes": len(gltf.get("nodes", [])),
        "meshes": len(gltf.get("meshes", [])),
        "primitives": sum(len(m.get("primitives", [])) for m in gltf.get("meshes", [])),
        "materials": len(materials),
        "textures": len(gltf.get("textures", [])),
        "images": len(gltf.get("images", [])),
        "samplers": len(gltf.get("samplers", [])),
        "accessors": len(gltf.get("accessors", [])),
        "animations": len(gltf.get("animations", [])),
        "skins": len(gltf.get("skins", [])),
        "cameras": len(gltf.get("cameras", [])),
        "lights": len((gltf.get("extensions", {}).get("KHR_lights_punctual", {}) or {}).get("lights", [])),
        "variants": len((gltf.get("extensions", {}).get("KHR_materials_variants", {}) or {}).get("variants", [])),
        "extensionsUsed": sorted(gltf.get("extensionsUsed", [])),
        "extensionsRequired": sorted(gltf.get("extensionsRequired", [])),
        "materialExtensions": dict(sorted(mat_ext.items())),
        "nodeExtensions": dict(sorted(node_ext.items())),
        "attributes": dict(sorted(attrs.items())),
        "primitiveModes": dict(sorted(modes.items())),
        "nodeNames": sorted(n["name"] for n in gltf.get("nodes", []) if "name" in n),
        "materialNames": sorted(m["name"] for m in materials if "name" in m),
        "animationNames": sorted(a["name"] for a in gltf.get("animations", []) if "name" in a),
        "cameraNames": sorted(c["name"] for c in gltf.get("cameras", []) if "name" in c),
        "bounds": position_bounds(gltf),
    }


SCALAR_KEYS = ["scenes", "nodes", "meshes", "primitives", "materials", "textures", "images", "samplers", "accessors",
               "animations", "skins", "cameras", "lights", "variants"]
SET_KEYS = ["extensionsUsed", "extensionsRequired", "nodeNames", "materialNames", "animationNames", "cameraNames"]
DICT_KEYS = ["materialExtensions", "nodeExtensions", "attributes", "primitiveModes"]


def diff(a, b, tol=1e-4):
    lines = []
    for k in SCALAR_KEYS:
        if a[k] != b[k]:
            lines.append("%-20s %s -> %s" % (k, a[k], b[k]))
    for k in SET_KEYS:
        sa, sb = set(a[k]), set(b[k])
        if sa != sb:
            lines.append("%-20s only in old: %s | only in new: %s" % (k, sorted(sa - sb)[:12], sorted(sb - sa)[:12]))
    for k in DICT_KEYS:
        if a[k] != b[k]:
            keys = sorted(set(a[k]) | set(b[k]))
            lines.append("%-20s %s" % (k, ", ".join("%s %s->%s" % (x, a[k].get(x, 0), b[k].get(x, 0)) for x in keys if a[k].get(x, 0) != b[k].get(x, 0))))
    ba, bb = a["bounds"], b["bounds"]
    if (ba is None) != (bb is None):
        lines.append("bounds               %s -> %s" % (ba, bb))
    elif ba is not None:
        for i, axis in enumerate("xyz"):
            for j, side in enumerate(("min", "max")):
                if abs(ba[j][i] - bb[j][i]) > tol * max(1.0, abs(ba[j][i])):
                    lines.append("bounds %s.%s        %.6g -> %.6g" % (axis, side, ba[j][i], bb[j][i]))
    return lines


def print_profile(p):
    print(p["file"], "(%d bytes)" % p["bytes"])
    for k in SCALAR_KEYS:
        print("  %-18s %s" % (k, p[k]))
    for k in ("extensionsUsed", "extensionsRequired", "materialExtensions", "nodeExtensions", "attributes", "primitiveModes"):
        print("  %-18s %s" % (k, p[k]))
    print("  %-18s %s" % ("bounds", p["bounds"]))


def summary(root, out_path):
    rows = []
    for dirpath, _dirs, files in os.walk(root):
        for name in sorted(files):
            if name.lower().endswith((".glb", ".gltf")):
                path = os.path.join(dirpath, name)
                try:
                    rows.append((os.path.relpath(path, root), profile(path)))
                except Exception as exc:  # report, do not stop
                    rows.append((os.path.relpath(path, root), {"error": str(exc)}))
    with open(out_path, "w", encoding="utf-8") as f:
        f.write("| File | Bytes | Nodes | Meshes | Materials | Textures | Anim | Cameras | Lights | Variants | Extensions used |\n")
        f.write("|---|---|---|---|---|---|---|---|---|---|---|\n")
        for rel, p in rows:
            if "error" in p:
                f.write("| %s | ERROR: %s |||||||||\n" % (rel, p["error"]))
            else:
                f.write("| %s | %d | %d | %d | %d | %d | %d | %d | %d | %d | %s |\n" % (
                    rel, p["bytes"], p["nodes"], p["meshes"], p["materials"], p["textures"], p["animations"],
                    p["cameras"], p["lights"], p["variants"], ", ".join(p["extensionsUsed"])))
    print("wrote", out_path, len(rows), "files")


def main(argv):
    if len(argv) >= 3 and argv[1] == "profile":
        for path in argv[2:]:
            print_profile(profile(path))
        return 0
    if len(argv) == 4 and argv[1] == "diff":
        lines = diff(profile(argv[2]), profile(argv[3]))
        print("identical (structure)" if not lines else "\n".join(lines))
        return 0 if not lines else 1
    if len(argv) == 4 and argv[1] == "summary":
        summary(argv[2], argv[3])
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
