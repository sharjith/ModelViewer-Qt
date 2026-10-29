#!/usr/bin/env python3
"""Generate a synthetic CAD-like assembly as a binary glTF (GLB) for interactive performance tests.

The Khronos NodePerformanceTest model is a flat list of 10k nodes. A production CAD assembly is a
deep tree of sub-assemblies with a few dozen parts each, which is what the scene tree, selection,
visibility and delete paths have to cope with. This builds such a tree: every part is its own
mesh with its own geometry (like imported CAD parts); names are neutral and made up.

Usage (output stays under out/, which is not tracked):
    python tools/perf/make_synthetic_assembly.py --meshes 35000 --out out/perf/synthetic_35k.glb

The result is deterministic for a given --seed.
"""
import argparse
import json
import math
import random
import struct


def box(sx, sy, sz):
    """24-vertex box centred on the origin: positions, normals, indices."""
    hx, hy, hz = sx / 2, sy / 2, sz / 2
    faces = [
        ((1, 0, 0), [(hx, -hy, -hz), (hx, hy, -hz), (hx, hy, hz), (hx, -hy, hz)]),
        ((-1, 0, 0), [(-hx, -hy, hz), (-hx, hy, hz), (-hx, hy, -hz), (-hx, -hy, -hz)]),
        ((0, 1, 0), [(-hx, hy, -hz), (-hx, hy, hz), (hx, hy, hz), (hx, hy, -hz)]),
        ((0, -1, 0), [(-hx, -hy, hz), (-hx, -hy, -hz), (hx, -hy, -hz), (hx, -hy, hz)]),
        ((0, 0, 1), [(-hx, -hy, hz), (hx, -hy, hz), (hx, hy, hz), (-hx, hy, hz)]),
        ((0, 0, -1), [(hx, -hy, -hz), (-hx, -hy, -hz), (-hx, hy, -hz), (hx, hy, -hz)]),
    ]
    pos, nrm, idx = [], [], []
    for n, quad in faces:
        base = len(pos)
        pos += quad
        nrm += [n] * 4
        idx += [base, base + 1, base + 2, base, base + 2, base + 3]
    return pos, nrm, idx


def cylinder(radius, height, segments):
    """Capped cylinder along Y with smooth side normals."""
    pos, nrm, idx = [], [], []
    h = height / 2
    for i in range(segments + 1):
        a = 2 * math.pi * i / segments
        c, s = math.cos(a), math.sin(a)
        pos += [(radius * c, -h, radius * s), (radius * c, h, radius * s)]
        nrm += [(c, 0, s)] * 2
    for i in range(segments):
        a0 = 2 * i
        idx += [a0, a0 + 2, a0 + 1, a0 + 1, a0 + 2, a0 + 3]
    for sign in (1, -1):
        centre = len(pos)
        pos.append((0, sign * h, 0))
        nrm.append((0, sign, 0))
        ring = len(pos)
        for i in range(segments):
            a = 2 * math.pi * i / segments
            pos.append((radius * math.cos(a), sign * h, radius * math.sin(a)))
            nrm.append((0, sign, 0))
        for i in range(segments):
            j = (i + 1) % segments
            idx += [centre, ring + i, ring + j] if sign > 0 else [centre, ring + j, ring + i]
    return pos, nrm, idx


def torus(major, minor, nu, nv):
    """Torus around Y with nu segments along the ring and nv around the tube: 2*nu*nv triangles."""
    pos, nrm, idx = [], [], []
    for i in range(nu + 1):
        u = 2 * math.pi * i / nu
        cu, su = math.cos(u), math.sin(u)
        for j in range(nv + 1):
            v = 2 * math.pi * j / nv
            cv, sv = math.cos(v), math.sin(v)
            pos.append(((major + minor * cv) * cu, minor * sv, (major + minor * cv) * su))
            nrm.append((cv * cu, sv, cv * su))
    for i in range(nu):
        for j in range(nv):
            a = i * (nv + 1) + j
            b = a + nv + 1
            idx += [a, b, a + 1, a + 1, b, b + 1]
    return pos, nrm, idx


def uvsphere(radius, lon, lat):
    """UV sphere: about 2*lon*(lat-1) triangles."""
    pos, nrm, idx = [], [], []
    for i in range(lat + 1):
        p = math.pi * i / lat
        for j in range(lon + 1):
            t = 2 * math.pi * j / lon
            n = (math.sin(p) * math.cos(t), math.cos(p), math.sin(p) * math.sin(t))
            pos.append(tuple(radius * c for c in n))
            nrm.append(n)
    for i in range(lat):
        for j in range(lon):
            a = i * (lon + 1) + j
            b = a + lon + 1
            if i > 0:
                idx += [a, b, a + 1]
            if i < lat - 1:
                idx += [a + 1, b, b + 1]
    return pos, nrm, idx


class Builder:
    def __init__(self):
        self.bin = bytearray()
        self.views, self.accessors, self.meshes, self.nodes = [], [], [], []

    def _view(self, data, target):
        while len(self.bin) % 4:
            self.bin.append(0)
        self.views.append({"buffer": 0, "byteOffset": len(self.bin), "byteLength": len(data), "target": target})
        self.bin += data
        return len(self.views) - 1

    def add_mesh(self, name, pos, nrm, idx, material):
        pmin = [min(p[i] for p in pos) for i in range(3)]
        pmax = [max(p[i] for p in pos) for i in range(3)]
        pv = self._view(b"".join(struct.pack("<3f", *p) for p in pos), 34962)
        nv = self._view(b"".join(struct.pack("<3f", *n) for n in nrm), 34962)
        iv = self._view(b"".join(struct.pack("<H", i) for i in idx), 34963)
        a0 = len(self.accessors)
        self.accessors += [
            {"bufferView": pv, "componentType": 5126, "count": len(pos), "type": "VEC3", "min": pmin, "max": pmax},
            {"bufferView": nv, "componentType": 5126, "count": len(nrm), "type": "VEC3"},
            {"bufferView": iv, "componentType": 5123, "count": len(idx), "type": "SCALAR"},
        ]
        self.meshes.append({"name": name, "primitives": [{
            "attributes": {"POSITION": a0, "NORMAL": a0 + 1}, "indices": a0 + 2, "material": material}]})
        return len(self.meshes) - 1

    def add_node(self, **kw):
        self.nodes.append(kw)
        return len(self.nodes) - 1


PART_KINDS = ["BRACKET", "SCREW", "NUT", "WASHER", "PIPE", "FLANGE", "GASKET", "STUD", "SHAFT", "COVER", "PLATE", "CLIP"]
ASM_KINDS = ["ASSEMBLY", "MODULE", "SUBFRAME", "MANIFOLD", "HARNESS", "CARRIER"]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--meshes", type=int, default=35000, help="number of part meshes (default 35000)")
    ap.add_argument("--out", default="out/perf/synthetic_35k.glb")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--segments", type=int, default=12, help="cylinder facets per part (default 12)")
    ap.add_argument("--tri-min", type=int, default=50, help="with --tri-max: smallest target triangles per part")
    ap.add_argument("--tri-max", type=int, default=0,
                    help="largest target triangles per part; 0 keeps the simple boxes/cylinders (default). "
                         "Parts mix cylinders, tori and spheres, log-uniform between --tri-min and --tri-max")
    ap.add_argument("--max-parts", type=int, default=60, help="largest number of parts directly under one assembly")
    ap.add_argument("--max-subs", type=int, default=8, help="largest number of sub-assemblies under one assembly")
    args = ap.parse_args()

    rnd = random.Random(args.seed)
    b = Builder()
    materials = [{"name": "MAT-%02d" % i, "pbrMetallicRoughness": {
        "baseColorFactor": [rnd.uniform(.2, .9), rnd.uniform(.2, .9), rnd.uniform(.2, .9), 1],
        "metallicFactor": rnd.choice([0.0, 1.0]), "roughnessFactor": rnd.uniform(.3, .8)}} for i in range(40)]

    remaining = [args.meshes]
    counter = [0]

    def make_part():
        counter[0] += 1
        n = counter[0]
        kind = rnd.choice(PART_KINDS)
        if args.tri_max > 0:
            # Target triangle count, log-uniform so there are many small parts and a few heavy ones.
            target = math.exp(rnd.uniform(math.log(max(args.tri_min, 12)), math.log(args.tri_max)))
            shape = rnd.choice(["cylinder", "torus", "sphere"])
            if shape == "cylinder":
                pos, nrm, idx = cylinder(rnd.uniform(.3, 2), rnd.uniform(.5, 6), max(3, int(target / 4)))
            elif shape == "torus":
                k = max(3, int(math.sqrt(target / 2)))
                pos, nrm, idx = torus(rnd.uniform(.8, 2.5), rnd.uniform(.2, .6), k, k)
            else:
                lon = max(4, int(math.sqrt(target)))
                pos, nrm, idx = uvsphere(rnd.uniform(.4, 2), lon, max(3, lon // 2))
        elif rnd.random() < .5:
            pos, nrm, idx = box(rnd.uniform(.5, 4), rnd.uniform(.5, 4), rnd.uniform(.5, 4))
        else:
            pos, nrm, idx = cylinder(rnd.uniform(.3, 2), rnd.uniform(.5, 6), args.segments)
        name = "PRT%06d/%03d-%s" % (n, rnd.randint(1, 9), kind)
        mesh = b.add_mesh(name, pos, nrm, idx, rnd.randrange(len(materials)))
        node = b.add_node(name=name, mesh=mesh,
                          translation=[rnd.uniform(-5, 5), rnd.uniform(-5, 5), rnd.uniform(-5, 5)])
        remaining[0] -= 1
        return node

    def make_assembly(depth):
        counter[0] += 1
        name = "ASM%05d/%03d-%s" % (counter[0], rnd.randint(1, 9), rnd.choice(ASM_KINDS))
        node = b.add_node(name=name, translation=[rnd.uniform(-20, 20), rnd.uniform(-20, 20), rnd.uniform(-20, 20)])
        children = []
        # Bushy near the top, no sub-assemblies below depth 7.
        subs = rnd.randint(0, args.max_subs) if depth < 7 else 0
        if depth < 2:
            subs = max(subs, 3)
        for _ in range(subs):
            if remaining[0] <= 0:
                break
            children.append(make_assembly(depth + 1))
        for _ in range(rnd.randint(1, args.max_parts)):
            if remaining[0] <= 0:
                break
            children.append(make_part())
        b.nodes[node]["children"] = children
        return node

    top = []
    while remaining[0] > 0:
        top.append(make_assembly(1))
    root = b.add_node(name="SYNTHETIC-ROOT", children=top)

    doc = {
        "asset": {"version": "2.0", "generator": "tools/perf/make_synthetic_assembly.py"},
        "scene": 0, "scenes": [{"nodes": [root]}], "nodes": b.nodes, "meshes": b.meshes,
        "materials": materials, "accessors": b.accessors, "bufferViews": b.views,
        "buffers": [{"byteLength": len(b.bin)}],
    }
    js = json.dumps(doc, separators=(",", ":")).encode()
    js += b" " * (-len(js) % 4)
    binary = bytes(b.bin) + b"\0" * (-len(b.bin) % 4)
    total = 12 + 8 + len(js) + 8 + len(binary)
    with open(args.out, "wb") as f:
        f.write(struct.pack("<4sII", b"glTF", 2, total))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(struct.pack("<II", len(binary), 0x004E4942) + binary)

    asm_nodes = sum(1 for n in b.nodes if "mesh" not in n)
    tris = sum(b.accessors[3 * i + 2]["count"] for i in range(len(b.meshes))) // 3
    print("wrote %s: %d meshes, %d assembly nodes, %d triangles, %.1f MB"
          % (args.out, len(b.meshes), asm_nodes, tris, total / 1e6))


if __name__ == "__main__":
    main()
