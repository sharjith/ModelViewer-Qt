# OCCT JT reader subset

Upstream: https://github.com/Open-Cascade-SAS/JT-Assistant
Commit: `e859e8c2d809f9d9468e6211eba0052f433978ee`.

The TKJT reader/decoder subset is used under its per-file GPL version 2-or-later permissions, choosing GPLv3 for this project. Original notices and the upstream GPLv2 license text are retained. The upstream viewer, Eigen copy, build scripts and commercial XT bridge are excluded. Dependencies are OCCT foundation/math, zlib and LibLZMA, supplied by the application's dependency build. Decoder threading is disabled with `NO_JT_MULTITHREADING`; no TBB dependency is introduced by this target.

ModelViewer changes: modern OCCT handles/RTTI and hash API compatibility; version-specific offsets and bounded input handling; diagnostics/cancellation and unsupported-representation handling. See the repository diff for the exact changes. This subset decodes display tessellation; it does not include the commercial Parasolid reader or claim exact B-Rep conversion.

Siemens JT data structures are used under the implementation permissions in the JT Data Format Reference. Copyright Siemens. The reference itself is not redistributed. See `docs/JT_IMPORT_FEASIBILITY.md` for the reference and patent-notice links and their scope.
