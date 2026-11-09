# How professional viewers handle very large models (survey)

Status: first pass, 2026-10-28. Web search only; the vendor user guides and the JT specification were not
read. Everything below is **Reported** (a source says so) or **Gap** (not found). Nothing here is measured
on this application.

## Reported practice

| Technique | Source and what it says | Relevance here |
|---|---|---|
| Load a lightweight format, not native CAD | Siemens positions VisMockup / Teamcenter Visualization on the JT format, "lightweight" for very complex assemblies ([Mockup fact sheet](https://media.plm.automation.siemens.com/teamcenter/Teamcenter2008_Web_English/PublishFolder/collateral/LV_Mockup_FS.pdf)). A third-party summary puts JT at roughly 1-10 % of native CAD size ([JT overview](https://cavac.at/cavacopedia/JT%20%28visualization%20format%29), secondary source). | Already our import route for JT/glTF. |
| Per-level simplified shells | Teamcenter pre-caches simplified shells of each assembly level so little data loads until the user drills down; detailed parts load on demand for tasks such as measuring or sectioning ([Teamcenter Visual Navigation and Reporting](https://www.mayahtt.com/wp-content/uploads/2013/03/Teamcenter%20Visual%20Navigation%20and%20Reporting.pdf)). | The basis for the planned on-demand loading branch. |
| LOD with progressive refinement and unloading | JT's LOD scheme: coarse overview model, finer detail loaded when zooming, non-visible representations unloaded to save memory ([cadinterop.com](https://cadinterop.com/en/formats/neutral-format/jt.html)). | Memory side of the large-assembly work (Vertex size, per-mesh copies). |
| Visibility control instead of loading everything | The Visualization Standard fact sheet says complete visibility control lets users work on extremely large assemblies "without having to load the whole product model" ([fact sheet](https://media.plm.automation.siemens.com/teamcenter/Teamcenter2008_Web_English/PublishFolder/collateral/LV_Standard_FS.pdf)). | Hide/Show Only and load-by-selection are the same idea. |
| Load assemblies as JT first, solids only where needed | Siemens guidance to open as JT until the working region is known, then load only those parts as solids ([digitalengineering247](https://digitalengineering247.com/article/working-with-large-cad-models)). | Same. |
| Occlusion culling, clustered LOD, out-of-core | Academic work: GPU out-of-core CAD rendering with compressed LODs plus occlusion culling ([paper record](https://ersearch2.cvtisr.sk/vufind/SummonRecord/FETCH-LOGICAL-c4344-91f96e3900bd1e177dc22e814ee512d87933905f9ca7e33ea5429804d36d50173/Details)); Quick-VDR clusters used for LOD, culling and fetching clusters a frame ahead ([KAIST record](https://koasas.kaist.ac.kr/handle/10203/87674?mode=full)). | Render-side; our measured problems so far are tree and selection, not draw cost. |
| Annotation data can dominate load time | A community report: a VF file grew from ~20 KB to 11 MB with markups/snapshots and load time from ~10 s to minutes (Mockup 8.3; [thread](https://community.sw.siemens.com/s/question/0D54O00006eL897SAC/loading-time-issues-and-vf-file-size-questions-in-tc-vis-mockup-and-others)). Anecdotal. | Caution for MVF sessions with many notes/states. |

## Gaps (not found; do not treat as known)
- How Mockup virtualises or lazily builds its structure tree, and what its selection and visibility cost model is.
- Occlusion culling, "Simplified" representation and large-model thresholds in Mockup.
- Navisworks, Creo View and 3DEXPERIENCE internals.
- JT segment layout used for lazy loading (needs the JT specification, ISO 14306).

To close the gaps: read the Siemens Visualization/Mockup user guide for the installed version, and the JT
specification's LSG/segment chapters; record the exact pages and versions here.

## What this suggests for this project (hypotheses, not decisions)
1. A tree that builds children when a node is first expanded, or a virtualised model/view tree, would remove
   the per-item cost that dominated the 34k trace. Not found in a vendor source; it is standard Qt practice.
2. Selection of a large node should stay a node-level operation (the viewer selects by subtree) rather than
   expanding to per-mesh work. The topmost-only walk moves in that direction.
3. On-demand loading by assembly (separate branch) matches the vendors' simplified-shell and load-as-needed model.
