# Wire and feature-edge vertex alpha — Gate C2b

21 October 2026, `feature/JT-importer`. **Owner accepted the viewer gate and authorized commit/push on 21 October 2026. The agent did not build or run the viewer.** C2a shaded raster transparency was owner-accepted and committed as `3523fcf`. No new JT revision or decoder layout is enabled.

## Scope and method

The dedicated wire shader previously used fixed coverage: 0.75 for standalone wires and 1 for edge overlays. It now multiplies that coverage by interpolated source vertex alpha when the mesh has vertex colours and an explicit Alpha/BLEND material. The ordinary triangle-wire and heuristic feature-edge paths both enable this mapping. This uses the same decoded RGBA and appearance decisions as C2a, across already qualified transports; it does not establish new RGBA codec coverage for every version.

An original per-draw scope selects premultiplied alpha blending and disables depth writes for these draws. It restores the prior blend enable, RGB/alpha factors, blend equations, depth-write flag and shader flag, including before an opaque mesh draws. The shader premultiplies the complete line coverage once. Inactive draws retain existing geometry-first wire appearance. Scalar material opacity, transmission, image alpha and analytic OCCT edge colour mapping remain outside this limited gate. Opaque JT and ONE/ZERO controls were normalized by the accepted adapter and retain ordinary wire coverage.

Pure-wire hover/selection still deliberately highlights geometry, including an otherwise invisible part. Edge overlays retain their existing contrast colours. Picking remains geometric; alpha does not remove source meshes or triangles. Source clip-plane rendering falls back to the existing main shader rather than the dedicated fast path. Ray-traced vertex alpha remains a separate C2 gate.

## Provenance and evidence limits

This pass consulted only existing ModelViewer code at `3523fcf`: the wire shader, its triangle/feature-edge draw callers, accepted RGBA fixture generator and viewport pass setup. No new external reference, vendor software, schema, SDK or third-party implementation was consulted. No vendored code, dependency, licence notice or enabled version changed. This records engineering method, not legal clearance. See [provenance](JT_PROVENANCE.md) and the [repository handout](../../RESEARCH_FOLDER_HANDOUT.md).

Read-only inspection and `git diff --check` are the agent checks. No new build, GPU run or automated pass is claimed. The owner normally builds/tests. Existing original cube controls remain ignored at `out/jt-research/vertex-alpha-ui-fixtures/`, with filenames and hashes from [C2a](JT_VERTEX_ALPHA_VALIDATION.md). No regeneration is required; that document gives exact optional regeneration commands. No generated file is added to Git.

## Owner build/viewer gate

Rebuild the viewer. Open each existing control separately, deselect it and move the pointer off the geometry. Check both **Wireframe** (feature edges) and **Hollow Mesh** (triangle wires), then **Shaded With Edges** and **Mesh Edges** (triangle edge overlay):

| Existing file | Wire / hollow expectations | Shaded edge expectations |
| --- | --- | --- |
| `alpha_8_2_le_opaque.jt` | Blue outer and orange inner edges use normal coverage | Opaque blue surface hides orange |
| `alpha_8_2_le_blend.jt` | Blue edges are fainter than the opaque reference; orange edges retain ordinary coverage | Orange is visible through blue, with partially transparent blue edges |
| `alpha_8_2_le_zero.jt` | No blue edges while deselected; orange edges remain | Only orange remains; no blue wire cage |
| `alpha_8_2_le_one_zero.jt` | Matches opaque wire coverage | Matches opaque surface/edges |
| `alpha_8_2_be_blend.jt` | Matches the little-endian blend control | Matches the little-endian blend control |
| `mixed_alpha_blend.glb` | Outer edge coverage varies with the 0.25/0.75 vertex alpha | Transparent outer surfaces/edges preserve the variation |

Unlike filled views, wire-only modes naturally expose the inner cube even when the outer cube's edges are opaque; judge blue-edge intensity and the zero case, not inner visibility alone. Reopen an ordinary opaque model to check that its wire appearance is unchanged. Select the zero-alpha outer cube from the tree: pure-wire selection may intentionally reveal its highlight; deselection must hide it again. Switch back to shaded raster mode to check accepted C2a behaviour. If a display-mode label differs in the local UI, use the corresponding triangle-wire or feature-edge mode.

The owner accepted this gate on 21 October 2026 and requested commit/push and proceeding. C1 residuals, ray-traced alpha, broader C2 resources/blend semantics and C3–C7 remain open. This closes the scoped C2b gate; broader material-only wire opacity/transmission remains outside its contract.
