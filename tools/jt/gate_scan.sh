#!/bin/bash
# Prints the JT exact-geometry completeness figures of every XT stream written by survey_xt_segments.py --extract-dir, one line per stream
# ("faces a/b, edges a/b, ..., capped holes n"), for tools/jt/gate_tally.py. Usage: tools/jt/gate_scan.sh <elems-dir> <out.txt>
# Needs the parasolid test tools built (out/parasolid-tests) and the vcpkg DLL folder, which this script adds to PATH.
cd "$(dirname "$0")/../.." || exit 1
export PATH="$PWD/out/build/ninja_release_vcpkg/vcpkg_installed/x64-windows/bin:$PATH"
dir=${1:?elements directory}
out=${2:?output file}
: > "$out"
for f in "$dir"/*.bin; do
  echo "$(basename "$f" .bin): $(timeout 120 ./out/parasolid-tests/parasolid_xt_shapes.exe --jt-binary "$f" 2>&1 | grep 'JT exact gate' | sed 's/JT exact gate: //')" >> "$out"
done
echo done
