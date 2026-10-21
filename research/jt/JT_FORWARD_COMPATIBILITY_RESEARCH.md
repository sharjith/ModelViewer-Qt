# JT 10.10/10.11 qualification research

20 October 2026, `feature/JT-importer`.

The owner authorized independent forward work while the 10.9 production-system UI check is pending. Commit `4cb6ebe` is pushed for that check. This authorization does not accept [Gate 2b](JT_109_PALETTE_VALIDATION.md); rendering corrections discovered there must still be resolved and regression-tested.

The owner subsequently selected the known 9.5 input for [Gate 3a](JT_95_VALIDATION.md) while forward-version inputs are unavailable. This changes the immediate implementation order without accepting Gate 2b or expanding the enabled forward versions.

## Evidence acquired

| Question | Primary evidence | Consequence for ModelViewer |
| --- | --- | --- |
| What public layout reference is available? | The [Siemens JT Open Program](https://www.siemens.com/en-us/products/plm-components/jt/jt-open-program/) still links JT 10.6 Revision D, dated 10 February 2026 | Use its documented layouts as a baseline; its revision date does not make it a 10.10/10.11 specification |
| Is 10.10 a real supported exchange version? | [Autodesk Inventor 2027 translator documentation](https://help.autodesk.com/cloudhelp/2027/ENU/Inventor-Help/files/GUID-AF41FA87-7588-4698-9C41-756A01EBE7F4.htm) lists JT import and export through 10.10 | A licensed exporter is a possible source of controlled fixtures; this table supplies no record-layout description |
| Is 10.11 shipping? | [Spatial's 23 April 2026 release announcement](https://blog.spatial.com/news/spatial-announces-the-general-availability-of-2026-1.0.1?hs_amp=true) lists JT 10.11 support; [RapidAuthor 2606 release notes](https://cortona3d.com/en/rapidauthor-2606-release-notes) also list it | Confirms the target's practical relevance, without establishing compatibility with our decoder |
| Do we have authentic matching inputs? | No 10.10/10.11 file has been acquired or header-verified in this pass; the owner has none locally | Native and UI qualification remain pending for these versions |

The public-reference search did not locate a 10.10 or 10.11 specification. This is a search result, not a claim that no such document exists. Vendor capability tables and release notes cannot establish byte-level compatibility. No restricted toolkit, leaked catalog or commercial translator implementation was acquired.

## Work independent of Gate 2b

1. Acquire authentic 10.10 and 10.11 parts and repeated-instance assemblies, with terms permitting ModelViewer testing. Record the unmodified 80-byte header, writer string, byte order, size, SHA-256, source and applicable terms outside the repository.
2. Compare container offsets, segment compression, element envelopes and versions, GUIDs, tessellation codecs, normal bindings and appearance records with the qualified modern family. Reuse parsers where the observed and documented layouts agree; add adapters only for demonstrated differences.
3. Build small original fixtures for each new layout, including both byte orders where applicable and malformed lengths/counts/indices. These are parser checks; changing a fixture's header alone does not qualify a real release.
4. Run isolated native validation against unchanged authentic inputs and retain prior-family regressions. Compare geometry counts, bounds, normals, instance transforms, material assignments and fallback behavior with an independent reference where available.
5. Present a separate owner gate with exact local paths and expected results. Update enabled versions, About and the README only when justified by the qualification evidence, recording any pending UI acceptance explicitly.

The existing 10.9 path already implements version-2 attribute tails, checked Sabot envelopes, palette accumulation, palette maps and per-triangle face groups. These are candidates for reuse, not proof that later files use identical layouts. The existing scene boundary remains indexed meshes, shared resources, local-transform occurrences and material groups; no evidence gathered here requires replacing it.

Checks that rely on the 10.9 visual result, including accepting its appearance mapping as a regression oracle, await Gate 2b. Source acquisition, byte-layout comparison and independent fixtures do not.

## Sample acquisition findings

Publicly listed samples were screened for testing restrictions. One source explicitly limited use to testing its own products and was excluded. Another candidate library could not be fetched successfully; no file from it was acquired or assumed to match a current version. Original candidate names and paths remain in ignored research notes. No downloaded geometry is added to this repository.

A useful controlled corpus would contain a single tessellated solid, a two-occurrence assembly with different local transforms and colours, and a part with multiple face materials, exported separately as 10.10 and 10.11. Include stored tessellation in every export; precise B-Rep is optional. If an appropriately licensed exporter is available, original geometry avoids dependence on community-model redistribution terms. Exporter availability and export options must be verified rather than assumed from a reader's support list.

Until authentic inputs or matching lawful layout evidence become available, 10.10/10.11 remain explicit unqualified versions. Stored-tessellation fallback continues to be the required display path; standalone XT and precise JT B-Rep translation remain separate work. See the [version policy](JT_VERSION_COMPATIBILITY.md) and [development phases](JT_DEVELOPMENT_PHASES.md).

## Latest owner confirmation — 20 October 2026

The owner confirmed successful UI testing of the supplied Solid Edge 10.9 assembly export. This supersedes earlier statements that all Gate 2b UI qualification is pending: sampled 10.9 display is accepted. The original production-system palette/face-group comparison remains pending additional coverage; the confirmation does not imply that every such variant was exercised. Gate 3b remains pending independently. Authentic 10.10 exports are now available, while 10.11 inputs remain unavailable. See [10.9 acceptance](JT_109_PALETTE_VALIDATION.md) and the [current expansion plan](JT_POINT_VERSION_EXPANSION.md).
