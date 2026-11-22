# Parasolid X_T research

15 November 2026, branch `feature/parasolid-importer`, based on `dev` at `694f8cd`.

Suggested reading order:

1. [October feasibility assessment](PARASOLID_IMPORT_RESEARCH.md) covers the format, standalone permissions, candidate readers and OCCT reconstruction.
2. [November X_T inventory and implementation gates](PARASOLID_XT_PHASES.md) records the prefix inventory, the first decoder target and the outstanding controls (Codex).
3. [Format coverage and local decode results](PARASOLID_XT_FORMAT_COVERAGE.md) records what the published reference specifies, where it differs from real files, and how far a text reader gets on the 12 local samples.
4. [Provenance and licensing statement](PARASOLID_PROVENANCE.md) lists every source consulted, with hashes, and what was and was not used.
5. [Staged implementation plan](PARASOLID_IMPLEMENTATION_PLAN.md) gives the architecture, stages X0–X8, owner gates and open decisions.
6. [Validation plan and acceptance table](PARASOLID_VALIDATION_PLAN.md) defines the checks, the per-sample acceptance table and the evidence still to be obtained.
7. [Stage X1 status and gate G1](PARASOLID_X1_STATUS.md) lists the reader sources written for the first stage and what the owner must run.
8. [JT and Parasolid implementation scope](../jt/JT_PARASOLID_IMPLEMENTATION_SCOPE.md) describes the proposed shared B-Rep boundary for a later embedded JT XT path.
9. [Standalone XT implementation and distribution assessment](PARASOLID_XT_IMPLEMENTATION_RIGHTS.md) reviews the supplied guide against primary sources and records the publication questions for the base layouts.
10. [Oversized periodic geometry and face loss](PARASOLID_PERIODIC_TRIM_INVESTIGATION.md) records source boundary measurements and the viewer validation still needed.
11. [OCCT STEP/IGES topology comparison](PARASOLID_OCCT_TOPOLOGY_COMPARISON.md) compares the local translator sources with XT and identifies the first authored-pcurve change.
12. [XT to OCCT 8 equivalence (BRepGraph spike)](PARASOLID_XT_OCCT_EQUIVALENCE.md) maps XT vertex/edge/fin/loop/face/shell/region to the OCCT 8 graph editor and records what a bottom-up build of the simple sample showed, notably that XT has no seam edges on periodic surfaces.

Where documents 2 and 5 differ on the first target (an exact schema key versus dispatch by transport form), document 5 records the local evidence for the change; the owner has not yet chosen.
