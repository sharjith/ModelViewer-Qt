# 3DXML importer provenance

Consolidated statement: 19 October 2026. This statement covers the development history through the initial native importer and supersedes any interpretation of an early research note as a branch-wide claim that only sample bytes were consulted. Earlier dated notes remain historical records.

## Binary UVR / 3DRep work

The binary layouts and codec behavior were inferred from structural measurements of the owner-supplied 3DXML archives, starting with ASM-GETRAG_V160.3dxml and extending to the later sample corpus. Research scripts and synthetic fixtures record the observations. No third-party UVR decoder code is recorded as used in this work. The published conceptual material consulted later was not a complete byte-level UVR codec specification.

The recorded workflow used no vendor SDK, executable disassembly or protection bypass. The integrity-certificate member was not used. These are statements about the recorded engineering method, not a legal clearance or a claim of a formally segregated clean-room process.

## XML tessellation and assembly semantics

The C++ XML TESSELLATED reader was developed from supplied XML samples and synthetic tests. The owner also supplied cad-in-unity as a reference. Its ParseReferenceRepUsecase.cs was consulted for XML tessellation concepts and finest-LOD selection. No C# source was copied, and Unity's handedness conversions were not adopted, as recorded in 3DXML_additional_layouts.md.

Reference project: [i2e-haw-hamburg/cad-in-unity](https://github.com/i2e-haw-hamburg/cad-in-unity), commit [43a9f0582ecafcdae5bb6f5919c0191467465f86](https://github.com/i2e-haw-hamburg/cad-in-unity/tree/43a9f0582ecafcdae5bb6f5919c0191467465f86). Its root licence is Apache-2.0. This is an acknowledgement of a consulted reference, not a statement that ModelViewer vendors that implementation or that its licence grants rights to Dassault technology or sample models.

## Documentation consulted later

The later specification-research pass consulted the following sources for XML and format concepts; the binary research did not remain the sole source for the entire branch:

- Dassault's [2005 specification publication announcement](https://www.3ds.com/newsroom/press-releases/dassault-systemes-delivers-3d-xml-specifications-and-player).
- A [third-party transcription of 3D XML User Guide 4.3](https://www.scribd.com/document/739651988/3D-XML-User-Guide-4-3); publisher authorization and completeness were not verified.
- [Vendor help mirrored by AES Vietnam](https://help-3dexperience.aesvietnam.com/English/ExchUserMap/exch-c-ImportExport-3dxml.htm); not a publisher-hosted specification download.
- The [xeokit 3DXML format guide](https://xeokit.github.io/sdk/docs/api/documents/formats.threedxml.3DXML_Format_Guide.html); documentation for a separate implementation, not the Dassault specification.

No vendor schema/specification package with accompanying implementation terms was acquired or audited. No such package, vendor manual, third-party transcription or vendor executable is redistributed here. Reading a public page is not recorded as establishing redistribution permission or comprehensive implementation/patent rights. See [specification research](3DXML_specification_research.md) for source limitations.

## Samples, attribution and remaining limits

Original supplied CAD models, loose-model companions and decoded geometry exports remain outside tracked test fixtures and release packaging. The GETRAG source identifies Alexander Yarets; this records source attribution and does not establish permission to redistribute the model. Obtain permission or applicable licence terms before distributing any third-party model. Committed tests use synthetic data; audit JSON files record measurements rather than mesh geometry.

The importer reads a measured subset of 3DXML. No Dassault certification, endorsement, affiliation, universal compatibility or non-infringement determination is claimed. Published-format access, copyright compliance, contractual terms and patents are separate questions. The same limits apply to commercial and noncommercial distribution.
