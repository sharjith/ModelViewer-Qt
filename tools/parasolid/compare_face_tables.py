#!/usr/bin/env python3
"""Compare the XT face table against a STEP reference of the same model.

Inputs (all produced by the standalone tools, under out/):
  --xt-tsv   parasolid_xt_shapes <layouts> <file.x_t> --mesh --face-table <this.tsv>
  --xt-log   the stdout of that run (per-body "volume=" and "area=" lines)
  --step-tsv parasolid_step_summary <file.step> <this.tsv>
Output: --out TSV with one row per XT face (matched STEP face area, verdict) and a printed summary.

XT faces are in part-local coordinates and STEP solids are placed, so matching uses only transform-invariant
quantities: face count, total area, and (surface type, area) per face.
Observations only: a mismatch says the XT face differs from the STEP face, not which one is right.
"""
import argparse
import collections
import csv
import re
import sys


def load_step(path):
    solids, faces = {}, collections.defaultdict(list)
    for r in csv.DictReader(open(path), delimiter="\t"):
        s = int(r["solid"])
        if r["kind"] == "solid":
            solids[s] = {"volume": float(r["volume"])}
        else:
            faces[s].append({"type": int(r["surface"]), "area": float(r["area"])})
    for s in solids:
        solids[s]["faces"] = faces[s]
        solids[s]["area"] = sum(f["area"] for f in faces[s])
    return solids


def load_xt(tsv, log):
    bodies = collections.defaultdict(list)
    for r in csv.DictReader(open(tsv), delimiter="\t"):
        bodies[r["body"]].append(r)
    meta = {}
    for line in open(log, errors="replace"):
        m = re.match(r"\s+body (\d+): faces (\d+)/(\d+) valid=(\d) free_edges=(\d+) volume=(\S+) area=(\S+)", line)
        if m:
            meta[m.group(1)] = {"volume": float(m.group(6)), "area": float(m.group(7))}
    return bodies, meta


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--xt-tsv", required=True)
    ap.add_argument("--xt-log", required=True)
    ap.add_argument("--step-tsv", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    step = load_step(a.step_tsv)
    bodies, meta = load_xt(a.xt_tsv, a.xt_log)
    by_count = collections.defaultdict(list)
    for s, d in step.items():
        by_count[len(d["faces"])].append(s)

    rows, nomatch = [], 0
    for b, faces in bodies.items():
        n = len(faces)
        xt_area = sum(float(f.get("area") or 0) for f in faces)
        cands = [s for k in range(n - 2, n + 3) for s in by_count.get(k, [])]
        if not cands:
            nomatch += 1
            continue
        best = min(cands, key=lambda s: abs(step[s]["area"] - xt_area) / max(xt_area, 1e-9))
        pool = collections.defaultdict(list)
        for f in step[best]["faces"]:
            pool[f["type"]].append(f["area"])
        for f in sorted(faces, key=lambda f: -float(f.get("area") or 0)):
            t = int(f.get("surftype") or -1)
            ar = float(f.get("area") or 0)
            cand = pool.get(t, [])
            if not cand or t < 0:
                rows.append((b, f["face"], f["surface"], f["origin"], f["brepcheck"], ar, None, "no-step-face-of-type"))
                continue
            j = min(range(len(cand)), key=lambda i: abs(cand[i] - ar))
            ref = cand.pop(j)
            ratio = ar / ref if ref > 1e-9 else float("nan")
            verdict = "ok" if 0.97 <= ratio <= 1.03 else ("too-small" if ratio < 0.97 else "too-big")
            rows.append((b, f["face"], f["surface"], f["origin"], f["brepcheck"], ar, ref, verdict))

    with open(a.out, "w") as out:
        out.write("body\tface\tsurface\torigin\tbrepcheck\txt_area\tstep_area\tverdict\n")
        for r in rows:
            out.write("\t".join("" if v is None else (f"{v:.6g}" if isinstance(v, float) else str(v)) for v in r) + "\n")
    differ = ("too-small", "too-big")
    print("faces compared:", len(rows), "| bodies without a STEP solid of similar face count:", nomatch)
    for key, c in collections.Counter(r[7] for r in rows).most_common():
        print(f"  {key:24s} {c}")
    print("faces whose area differs >3% from the nearest STEP face of the same type, by surface:")
    tot = collections.Counter(r[2] for r in rows)
    for k, c in collections.Counter(r[2] for r in rows if r[7] in differ).most_common(10):
        print(f"  {k:14s} {c}/{tot[k]}")
    print("by origin:")
    totO = collections.Counter(r[3] for r in rows)
    for k, c in collections.Counter(r[3] for r in rows if r[7] in differ).most_common(8):
        print(f"  {k:32s} {c}/{totO[k]}")
    for chk in ("valid", "invalid"):
        sel = [r for r in rows if r[4] == chk]
        print(f"BRepCheck {chk}: {sum(1 for r in sel if r[7] in differ)}/{len(sel)} differ")
    print("bodies with most differing faces:", collections.Counter(r[0] for r in rows if r[7] in differ).most_common(8))


if __name__ == "__main__":
    sys.exit(main())
