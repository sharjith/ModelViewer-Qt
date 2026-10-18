# 3DXML published specification research

Research date: 2026-10-18. This pass changes documentation only.

## Finding

Published 3DXML documentation exists. It is useful for replacing assumptions in
the XML reader with documented rules. This search did not locate a public,
complete byte-level CFV3/UVR codec specification. That is a search result, not
proof that Dassault has never published or licensed one. Universal compatibility
cannot be inferred from either the XML schema or the five successful samples.

## Sources and availability

1. [Dassault's specification publication announcement, June 15, 2005](https://www.3ds.com/newsroom/press-releases/dassault-systemes-delivers-3d-xml-specifications-and-player).
   Confirms that specifications were released. Its download link currently
   redirects to the general software-download page; it does not deliver the
   original specification package directly.
2. [3D XML User Guide 4.3, Edition 0, March 2009](https://www.scribd.com/document/739651988/3D-XML-User-Guide-4-3).
   Vendor-authored document, inspected through a third-party transcription;
   publisher download and completeness not verified. Covers assemblies,
   representation links, graphic properties, mesh data, packaging and versions.
   It distinguishes XML TESSELLATED 1.2 from UVR 1.0, but the inspected text is
   not a UVR byte-stream codec specification. It points to separate Reference
   Documentation and schemas.
3. [Dassault software downloads](https://www.3ds.com/support/software-downloads),
   leading to the [download platform](https://software.3ds.com/).
   This is the current official route. The research browser could not enumerate
   an actual XSD download package or retrieve a current publisher-hosted guide.
   Do not report the schema package as acquired or audited.
4. [About 3D XML, vendor help mirrored by AES Vietnam](https://help-3dexperience.aesvietnam.com/English/ExchUserMap/exch-c-ImportExport-3dxml.htm).
   Identifies the requested download category as **3D XML XSD Schema**, and
   directs readers to Dassault's former downloads page. This is a mirrored
   vendor document, not a current official download endpoint.
5. [3DXML import/export on 3DEXPERIENCE R2024x](https://www.3ds.com/support/documentation/resource-library/3dxml-import-export-3dexperience-r2024x).
   Current official best-practices resource about exchange workflows. Its page
   describes platform operations; it is not evidence of a published binary
   geometry codec. The linked full PDF was not inspected in this pass.
6. [Xeokit developer's 3DXML format guide](https://xeokit.github.io/sdk/docs/api/documents/formats.threedxml.3DXML_Format_Guide.html).
   First-party documentation for that implementation, not the Dassault format
   specification. It explicitly describes a subset and omits several rendering
   features. Useful for comparison, not for claiming complete UVR coverage.

A historical download link found in a developer discussion also redirects to
Dassault's generic download page. Names mentioned there include `3DXML.xsd`
and `3DXMLMesh.xsd`. Treat filenames as acquisition leads until an authentic
package, its version, imports and accompanying terms have been inspected.

No gated material was retrieved, no account was created and no vendor document
or schema package is redistributed in this repository. Public availability does
not by itself establish redistribution or implementation licence terms; those
need to be read with the actual package. This is a provenance note, not a legal
assessment of implementation rights.

## What this means for our importer

The following is an engineering work list from our current implementation and
its known limits. It is not a claim that an unavailable schema confirms each
proposed behavior.

| Area | Current basis | Next evidence needed |
|---|---|---|
| Product structure and XML geometry | Supplied samples, independent tests, published conceptual guide | Versioned XSDs and full tag/reference documentation |
| Optional fields and defaults | Narrow measured layouts; e.g. every Instance3D currently requires a 12-number matrix | Schema cardinalities, defaults and semantic constraints |
| Link resolution and ID scope | One product document; archive-local representation filenames; globally checked IDs within parsed document | Rules for IDs, qualified URNs, fragments and multiple product documents |
| Occurrence appearance and placement | Visibility implemented; broader default-view overrides incomplete | Documented precedence and occurrence addressing |
| Representation selection | UVR and XML TESSELLATED decoded; TECHREP skipped | Format/version registry and rules for selecting alternative representations |
| XML mesh variants | Lists/strips/fans, vertex normals, finest-face selection | Full mesh schema; allowed bags, bindings and LOD-only cases |
| Binary container and geometry | Complete consumption of measured sample layouts | CFV3/UVR record grammar, versions, compression, scalar meanings and normal quantization |
| Materials and units | Face RGB plus simple defaults; source units inferred | Authoritative property definitions, inheritance and unit semantics |

## Recommended next research / implementation sequence

1. Obtain the publisher's **3D XML XSD Schema** package, **Reference
   Documentation** and **User Guide**, recording exact versions, original URLs,
   hashes and terms. If the download platform requires account access, the
   owner can obtain the package through their account; do not bypass it.
2. Audit XML behavior against that package before making more broad changes.
   Build a traceable table: schema/type/rule, importer function, supported
   behavior, fixture and known exception. Keep unsupported content explicit.
3. Add a version-aware document resolver and representation dispatcher. Keep
   product-schema version, representation format and representation version
   separate. Detect payload encoding as well as reading metadata; never assume
   a filename extension alone determines XML versus binary.
4. Prioritize rendering completeness: placed geometry and normal bindings,
   representation-local structure/transforms, reference resolution and LOD
   selection. Continue treating colors/materials as secondary, per owner scope.
5. For binary coverage, ask Dassault for a published or appropriately licensed
   specification covering CFV3, UVR geometry records and the stored-normal
   codec. The public announcement and XML schemas alone do not answer these
   questions. Do not send a request externally without owner authorization.
6. Maintain a corpus organized by producer, producer release, product schema,
   representation version and export mode. Require complete structural reads,
   valid indices/normals, actual scene attachments and source/reference geometry
   checks. Do not silently reinterpret unknown record layouts.

A useful near-term target is **documented XML conformance plus explicitly
versioned binary coverage**. A claim to support every 3DXML case would need
substantially more evidence than is presently available.
