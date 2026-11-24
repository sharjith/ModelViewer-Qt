#!/bin/bash
# Runs parasolid_xt_shapes --jt-binary on every XT stream written by survey_xt_segments.py --extract-dir and
# writes one summary row per stream. Usage: tools/jt/run_xt_elements.sh <elems-dir> <out.tsv>
# Needs the parasolid test tools built (out/parasolid-tests) and the vcpkg DLL folder, which this script adds to PATH.
cd "$(dirname "$0")/../.." || exit 1
export PATH="$PWD/out/build/ninja_release_vcpkg/vcpkg_installed/x64-windows/bin:$PATH"
dir=${1:?elements directory}
out=${2:?output tsv}
printf 'stream\tdecode\tfaces_built\tfaces_total\tbodies\tbodies_valid\tfree_edge_bodies\tmesh_rejected\tseconds\n' > "$out"
for f in "$dir"/*.bin; do
  t0=$(date +%s)
  r=$(timeout 180 ./out/parasolid-tests/parasolid_xt_shapes.exe --jt-binary "$f" --mesh 2>&1)
  t1=$(date +%s)
  decode=$(echo "$r" | sed -n 's/^decode: \([A-Za-z]*\).*/\1/p' | head -1)
  fb=$(echo "$r" | sed -n 's/^faces: \([0-9]*\) built of \([0-9]*\).*/\1/p' | head -1)
  ft=$(echo "$r" | sed -n 's/^faces: \([0-9]*\) built of \([0-9]*\).*/\2/p' | head -1)
  sm=$(echo "$r" | sed -n 's/^summary: bodies \([0-9]*\), BRepCheck valid \([0-9]*\), bodies without free edges \([0-9]*\).*/\1 \2 \3/p' | head -1)
  mr=$(echo "$r" | sed -n 's/^viewer usable-triangulation check rejects \([0-9]*\) faces.*/\1/p' | head -1)
  set -- $sm
  nb=${1:-}; nv=${2:-}; nf=${3:-}
  [ -n "$nb" ] && [ -n "$nf" ] && nf=$((nb - nf))
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$(basename "$f" .bin)" "${decode:-crash/timeout}" "${fb:-}" "${ft:-}" "$nb" "$nv" "$nf" "${mr:-}" "$((t1 - t0))" >> "$out"
done
echo done
