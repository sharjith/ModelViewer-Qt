# Parasolid X_T duct handoff

Continue on `feature/parasolid-importer` in the existing working tree. Read [repository rules](../../RESEARCH_FOLDER_HANDOUT.md) and [X3 status](PARASOLID_X3_STATUS.md) first. Preserve the current changes and the local evidence in ignored `out/`. No commit or push was requested.

## Current result

The owner confirmed that the exterior rib and small hole are fixed on isolated duct body 116. Their final two close-ups show remaining broad folds, sharp diagonal facets and slivers near interior ribs. The affected source face IDs have yet to be identified. Use those images as visual evidence. The existing exterior screenshot alignment is unsuitable for the cropped interior views.

The installed build from `xt-duct116-marked-swept` meshed all 182 source faces. The B-Rep audit counted 50 free nondegenerate non-seam edges, and the mesh audit counted 265 unpaired triangle edges at 0.001 mm coordinate rounding. The earlier baseline had 84 and 425 respectively. Face count measures coverage and can include faces made by boundary filling. Full-assembly regression since the duct changes is pending.

The exterior repair used bounded charts on doubly closed spline faces 636768 and 636640. Leave those selections in the isolated validation run. A broad chart experiment changed another face, so generic chart selection still needs regression coverage. The opt-in circular-sweep blend reconstruction also recovered 633009, 633369, 633331 and 633337 in this duct.

## Where to start

1. Trace the two interior defects to emitted faces. Compare the current B-Rep and per-face mesh against the local reference and correlate with the native stderr trace. Inspect triangle extent, source carrier distance, pcurve branch and shared-edge incidence. The screenshots alone cannot distinguish a folded reconstructed surface from a bad trim or triangulation.
2. Start with shared source edge **633456**. Native trim rejects face **633077** at a 6.34428 mm sampled residual and **633140** at 5.34164 mm on that same edge. This is a strong lead, but the screenshot mapping remains open. Also check 632908/632930 (8.00553 mm), 637139/633734 (2.21419 mm) and 632915/632922 (0.791872 mm) if their extents match the marked locations. These are individual trace entries, not measured bounds.
3. Read the source intersection curve for that edge, its defining surfaces and chart, and both adjacent fin directions and parameter ranges. Compare the native 3D edge with the authored trim and the carrier on both sides. The existing `rebuildSplineBlendRail` in [XtOcct.cpp](../../src/Import/Parasolid/XtOcct.cpp) repairs certain open spline/blend intersections and already resolved an earlier large exterior triangle. Establish which branch diverges before changing its applicability. Also inspect `blendSurfaceForFace`, `faceForImpl` and `fillFaceFromBoundary`. Prefer a locally qualified shared curve or correct chart over enlarging tolerances. Earlier attempts to reuse original edges on approximate fills or to sew the assembled result did not yield a valid B-Rep.
4. Compare to the local parasolid-kit 0.3.17 run. It built 181 of 182 faces and had 15 free nondegenerate edges. It dropped 633077 after its fin-to-edge deviation exceeded the conversion limit, so use the XT source and neighboring trims to validate that face. The kit output is a comparison aid, with provenance in [the source record](PARASOLID_PROVENANCE.md).
5. Validate the first targeted change on body 116. Compare the actual affected face trims, mesh, and B-Rep incidence with the saved run. Check the recovered exterior features and the old giant triangle before running the full assembly. Keep one viewer instance and one relevant model per visual iteration. Use the full CBR assembly, XS650 and B4T for later regressions.

## Local reproduction

Run from the repository root. The diagnostic launcher resolves the sample on this workstation and sets the isolated body filter itself. Verify the current PID before supplying it. It was 40940 at handoff. Use 0 if no viewer is running.

```powershell
$r = 'out/parasolid-research'
$label = 'xt-duct116-interior'
$env:XT_TRY_BLEND_SECTION = '1'
$env:XT_TRY_OPEN_BLEND_PIPE = '1'
$env:XT_TRY_DUAL_CHART_FACES = '636768,636640'
$env:XT_TRACE_BODY_BREP = Join-Path (Resolve-Path $r).Path "$label.brep"
Get-Process ModelViewer -ErrorAction SilentlyContinue | Select-Object Id, Path
$viewerPid = 40940 # Replace with the verified process ID, or 0.
& "$r/run-xt-duct.ps1" -OldPid $viewerPid -Label $label
```

After import finishes, run the same checks against the saved run or the new label.

```powershell
$r = 'out/parasolid-research'
$p = "$r/xt-duct116-marked-swept"
$python = "$r/parasolid-kit-venv/Scripts/python.exe"
& $python "$r/xt-duct-brep-incidence.py" "$p.brep" "$p.stderr.log"
& $python "$r/xt-duct-dump-mesh.py" "$p.brep" "$p.json"
python "$r/xt-mesh-incidence.py" "$p.json"
rg 'trim residual face=(633077|633140|632908|637139|632915)\b' "$p.stderr.log"
```

The local reference artifacts are `parasolid-kit-duct.brep`, `parasolid-kit-duct.json` and `parasolid-kit-duct-face-map.json` in the same directory. Use the map to relate reference B-Rep face ordinals to source nodes. The local `xt-duct-rib-wire-compare.py` and `xt-duct-rib-interior-compare.py` scripts show how to compare the UV trims and sample distances. The decoder setup in `xt-exhaust-audit.py` can be reused to inspect original XT records with `layouts-v13.json`. Keep any scratch script and output under `out/`.

This handoff records the latest visual feedback and verified trace evidence. No importer edits or new build were made for the remaining interior folds.
