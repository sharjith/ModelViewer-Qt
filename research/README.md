# Research

Written research behind feature implementations. This folder holds **documents only**.

| Folder | Contents |
|---|---|
| [`3dxml/`](3dxml/) | 3DXML importer: format research, mesh and scene-graph specifications, provenance statement, decoding handout |

## What does not belong here

- Sample or third-party data of any kind (3DXML, OBJ/GLB exports, stream dumps, extracted payloads, preview images of third-party geometry). Redistribution rights are generally not known.
- Generated measurement output (JSON audits, logs, build products). Documents cite such artifacts as `out/...`; `out/` is untracked and the artifacts are regenerated locally with the commands given in each document.
- Scripts and tests. Analysis tooling lives in `tools/` (for example `tools/3dxml`), regression code in `tests/`.
- Routine audits (tooltips, icons and similar), unless they record a novel finding.

Provenance: [`3dxml/3DXML_PROVENANCE.md`](3dxml/3DXML_PROVENANCE.md) and the notices in [`../THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md).
