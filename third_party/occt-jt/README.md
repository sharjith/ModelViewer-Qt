# OCCT JT reader subset

Upstream: https://github.com/Open-Cascade-SAS/JT-Assistant
Commit: `e859e8c2d809f9d9468e6211eba0052f433978ee`.

The TKJT reader/decoder subset is used under its per-file GPL version 2-or-later permissions, choosing GPLv3 for this project. Original notices and the upstream GPLv2 license text are retained. The upstream viewer, Eigen copy, build scripts and commercial XT bridge are excluded. Dependencies are OCCT foundation/math, zlib and LibLZMA, supplied by the application's dependency build. Decoder threading is disabled with `NO_JT_MULTITHREADING`; no TBB dependency is introduced by this target.

ModelViewer changes: modern OCCT handles/RTTI and hash API compatibility; version-specific offsets and bounded input handling; diagnostics/cancellation and unsupported-representation handling. See the repository diff for the exact changes. This subset decodes display tessellation; it does not include the commercial Parasolid reader or claim exact B-Rep conversion.

Siemens JT data structures are used under the implementation permissions in the JT Data Format Reference. Copyright Siemens. The reference itself is not redistributed. See `docs/JT_IMPORT_FEASIBILITY.md` for the reference and patent-notice links and their scope.

## Dated modifications and distribution notices

ModelViewer adaptations were made on 18 October 2026; the dated notice/provenance pass is 19 October 2026. See [MODIFICATIONS.md](MODIFICATIONS.md) and [PROVENANCE.json](PROVENANCE.json). The manifest distinguishes 104 upstream-derived files from seven original decoder source/header files. Original upstream permissions remain intact.

[Importer notices](../../THIRD_PARTY_NOTICES.md) identify the Open CASCADE source and Siemens reference copyrights. The installer carries those notices, this README, the modification summary, provenance manifest and upstream GPLv2 text; the application GPLv3 text is also installed. About includes importer credits and an embedded notices viewer. Binary release preparation must separately satisfy GPLv3 Corresponding Source requirements for the actual distributed work. These documentation changes do not resolve patent clearance or expand Siemens permissions.
