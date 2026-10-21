# Research

Written research behind feature implementations. This folder holds **documents only**.

| Folder | Contents |
|---|---|
| [`jt/`](jt/JT_RESEARCH_INDEX.md) | JT importer: feasibility and licensing, version compatibility, codec and appearance investigations, validation records, Parasolid/JT scope |
| [`3dxml/`](3dxml/) | 3DXML importer: format research, mesh and scene-graph specifications, provenance statement, decoding handout |
| [`parasolid/`](parasolid/) | Standalone Parasolid (XT) import assessment |

## What does not belong here

- Sample or third-party data of any kind (JT, 3DXML, OBJ/GLB exports, stream dumps, extracted payloads, preview images of third-party geometry). Redistribution rights are generally not known.
- Generated measurement output (JSON audits, logs, build products). Documents cite such artifacts as `out/...`; `out/` is untracked and the artifacts are regenerated locally with the commands given in each document.
- Scripts and tests. Analysis tooling lives in `tools/` (for example `tools/3dxml`), regression code in `tests/` (for example `tests/jt`).
- Routine audits (tooltips, icons and similar), unless they record a novel finding.
- Compliance records that are not feature research; `docs/IMPORTER_NOTICE_VALIDATION.md` stays in `docs/`.

Provenance for each importer: [`3dxml/3DXML_PROVENANCE.md`](3dxml/3DXML_PROVENANCE.md) and the notices in [`../THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md).
