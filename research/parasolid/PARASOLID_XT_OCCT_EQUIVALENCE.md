# XT to OCCT 8 topology and geometry equivalence (BRepGraph spike)

16 November 2026, branch `feature/parasolid-importer`. Status: spike, standalone tool only. Nothing in the viewer path uses it.

## Provenance and method

OCCT 8.0.1, local vcpkg source tree and installed headers (same release and tarball hash as [PARASOLID_OCCT_TOPOLOGY_COMPARISON.md](PARASOLID_OCCT_TOPOLOGY_COMPARISON.md)). The new bottom-up builder is the `BRepGraph` editor (`BRepGraph_EditorView.hxx`, `BRepGraph_Validate.hxx`, `BRepGraphInc` storage); its own tests (`BRepGraph_ShapesViewImport_Test.cxx`) show the creation order used here. The earlier 7.9.1 samples are not the version this project links.

The spike is `Builder::buildBodyViaGraph` in [XtOcct.cpp](../../src/Import/Parasolid/XtOcct.cpp), enabled with the environment variable `XT_GRAPH=1`. It reuses the existing surface and edge-curve builders and replaces only the topology assembly: vertices, edges, coedges, wires, faces, shells and solids are created through the graph editor, validated with `BRepGraph_Validate`, then reconstructed to a `TopoDS` shape. 2D curves are projections (`GeomProjLib::Curve2d`), unwrapped along each loop. The same `SameParameter`/`UpdateTolerances` finishing as the direct builder is applied (`XT_GRAPH_RAW=1` skips it).

Regenerate (owner or agent, standalone target only; output under `out/`):

```powershell
$env:XT_GRAPH = '1'
out\parasolid-tests\parasolid_xt_shapes.exe out\parasolid-research\layouts-v13.txt <sample.x_t> --mesh > out\parasolid-research\scratch\graph.txt
```

Run the same command without `XT_GRAPH` for the direct builder.

## Equivalence table

| XT | OCCT 8 graph editor | Observed rule |
| --- | --- | --- |
| VERTEX (point, tolerance) | `Vertices().Add(point, tol)` | Metres scaled to mm; tolerance floored at the modeller resolution. Shared by identity. |
| EDGE (curve, two vertices via fins) | `Edges().Add(v0, v1, curve3d, first, last, tol)` | The parameter range comes from projecting the vertices on the curve (periodic curves wrap, reversed when the vertices run against the curve). A closed edge uses one vertex twice. Ring edges (no vertices in XT) need one created vertex. |
| FIN (edge use in a loop) | CoEdge: edge, parity orientation, 2D curve | Orientation is the fin sense (`+` forward). The 2D curve belongs to the coedge (face-specific), not to the edge. |
| LOOP (fin ring, face on the left of the XT normal) | Wire = ordered, connected CoEdges (`CheckCoEdgeOrder`) | A disconnected ring is rejected at creation, which is where a missing or merged edge shows. |
| FACE (surface, sense, loops) | `Faces().Add(surface, outer, inners, tol)` | XT has no outer/inner flag; the outer wire is the loop of largest absolute parametric area. |
| SHELL face / front_face | `Shells().Append(face, parity)` | Orientation = (XT face normal against OCCT natural normal) XOR (front-face list). |
| REGION type `S` | Solid; other regions are skipped when a solid exists | The void region of a solid body repeats its faces (counting them again doubled the faces in the first run). |

Orientation rule (observation, simple sample, 512 faces): with the XT loop direction kept and loops flipped (orientation inverted and order reversed) exactly when `xtNormal * flip < 0`, the outer loop has positive area in (u, v) for every plane, cylinder, cone, torus and spline face except 8 fitted blend faces. Those 8 are the same 8 the direct builder corrects by area. So the sense rule is sufficient for analytic surfaces and the area test is needed only where the parametrisation direction is not known in advance.

## What the spike showed

Simple sample (28 solid bodies, 518 faces), graph path against the direct path:

| Measure | Direct builder | Graph spike |
| --- | --- | --- |
| Faces built | 518 | 512 (4 edge, 2 micro-edge) |
| Graph validation errors | n/a | 0 |
| Faces valid (BRepCheck) | 503 | 382 |
| Faces triangulated | 518 | 429 |

- The graph editor accepts everything the importer produces and `BRepGraph_Validate` reports no structural errors (observation).
- All planar faces are valid and mesh. Every remaining invalid or untriangulated face is on a periodic surface (cylinder 39, cone 28, torus 4) or is one of the 8 fitted blends whose orientation was not area-corrected in the spike (observation).
- None of the 236 faces on closed surfaces (cylinder, cone, torus, periodic spline) has a seam edge in XT: no edge is used twice by any face. 238 loops on those faces wind around the surface (the 2D curve does not return to its start). OCCT needs a periodic face with a winding loop to be a topological disk, with an explicit seam edge used twice. XT stores two circular loops and leaves the seam implicit.
- The six faces lost in the spike are edges the direct builder drops (4) and micro-edges that the direct builder merges by vertex (2); the graph path does not merge vertices yet.

## Hypotheses (not yet tested)

1. A seam inserted for every winding-loop face (a straight or iso-parametric edge between a vertex on each loop, used twice with the u-period offset) would bring the periodic faces to parity. The direct builder gets the same result with `ShapeFix_Face::FixMissingSeam`; the graph route would create the seam edge and four coedges itself.
2. Micro-edge merging belongs at vertex creation (one graph vertex for both ends) rather than as a reshape afterwards.
3. Outer-wire selection by area should happen before `Faces().Add`, so the outer loop is first.

## Seam insertion result (16 November 2026)

Hypotheses 1 and 3 were implemented in the graph spike: an iso-parametric seam edge, used twice with the period offset, joins the two winding loops of a periodic face, and the outer loop (largest parametric area) is passed first. Same simple sample, same command (`XT_GRAPH=1`, `--mesh`):

| Measure | Direct builder | Graph, before seams | Graph, with seams |
| --- | --- | --- | --- |
| Faces built | 518 | 512 | 512 |
| Faces valid (BRepCheck) | 503 | 382 | 481 |
| Faces triangulated | 518 | 429 | 494 |
| Seams inserted | n/a (ShapeFix) | 0 | 100 (cone 26, cylinder 55, torus 19) |

Observations: the seam edge is the missing piece for the periodic faces that have two winding loops; 100 faces moved from failing to valid. The graph path is still behind the direct builder. The faces still without a seam, by the spike's own counters: 39 with one winding loop and nothing else (probably apex or pole faces that need a degenerate edge), 12 with no vertex at the seam angle on the second loop (an edge split would create one), 5 whose loops are not opposed in u, plus 4 dropped edges and 2 micro-edges that the graph path does not merge. Owner acceptance: none. The viewer still uses the direct builder.

## Literature check (16 November 2026)

Sources: installed OCCT 8.0.1 `ShapeFix_Face.hxx` (local), the [OCCT shape healing guide](https://dev.opencascade.org/doc/overview/html/occt_user_guides__shape_healing.html), and OCCT tracker issues [25553](https://tracker.dev.opencascade.org/view.php?id=25553), [24983](https://tracker.dev.opencascade.org/view.php?id=24983), [30419](https://tracker.dev.opencascade.org/view.php?id=30419) and [30831](https://tracker.dev.opencascade.org/view.php?id=30831). A web search for how other kernels import XT periodic faces found no primary description; vendor pages (for example [HOOPS Exchange](https://docs.techsoft3d.com/hoops/exchange/start/format/parasolid_reader.html)) list supported surfaces only. Nothing here comes from a Parasolid-side source.

Observations:

- The OCCT guide describes the XT situation exactly: a face on a periodic surface bounded by wires that close in 3D but not in parameter space gets a seam edge inserted by healing. `ShapeFix_Face::FixMissingSeam` merges two such wires into one and adds the seam edge. So the direct builder's repair is the intended mechanism, and the graph-path seam insertion reproduces it by hand.
- `FixPeriodicDegenerated` (default off) is the documented repair for a single wire belting a periodic surface at a pole, and the guide says it must run before `FixMissingSeam`. The importer enables neither it nor `FixSplitFace` for cones; it special-cases spheres only. Untested hypothesis: enabling it for cone apex and torus-belt faces would recover part of the single-winding-loop failures.
- Reported limits of `FixMissingSeam`: it can fail on faces with two wires of two edges each (25553), it does not add a seam when a seam edge already exists (24983), it copies and trims B-spline edges instead of splitting them (30419), and it can be very slow on thin faces (30831). Slow seam creation on thin faces is a candidate for the B4T hang, alongside the fills. Not yet checked against the B4T trace.
- `ShapeFix_Face::SplitEdge` exists, so a seam vertex missing on the second loop can be created by splitting an edge. The graph spike currently counts those faces as skipped instead.
- Nothing found on `BRepFill_Filling` runaway surfaces. The fill guards (mesh check, plane fallback) remain our own measures.

## Not covered

Timing at scale (the large samples), fills, display candidates and the blend-orientation area flip are not part of the spike. Owner acceptance: none.
