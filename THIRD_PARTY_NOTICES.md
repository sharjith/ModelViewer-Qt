# Importer third-party notices

Updated 19 October 2026. These notices supplement the application's existing library and asset notices; they are not an exhaustive list of every dependency. ModelViewer-Qt is distributed under GNU GPL version 3; see the accompanying gpl-3.0.txt and the corresponding source repository at https://github.com/sharjith/ModelViewer-Qt. Copyright and licence notices for other components remain in their supplied files.

## 3DXML reference acknowledgement

The native 3DXML importer is ModelViewer-Qt code. Its XML tessellation research consulted the Apache-2.0-licensed cad-in-unity project by the i2e-haw-hamburg contributors:
https://github.com/i2e-haw-hamburg/cad-in-unity/tree/43a9f0582ecafcdae5bb6f5919c0191467465f86

No cad-in-unity C# source is bundled. This acknowledgement does not apply that project's licence to the 3DXML format or to its example CAD models. The full research provenance is recorded in docs/3DXML_PROVENANCE.md in the source distribution.

3DXML, CATIA and Dassault Systemes names identify the corresponding third-party format or products. ModelViewer-Qt is not certified, sponsored or endorsed by Dassault Systemes. No vendor schema, manual or proprietary decoder is bundled by this importer.

## Open CASCADE JT-Assistant reader subset

Copyright (C) 2001-2015 OPEN CASCADE SAS; the applicable years vary by file and are retained in each upstream-derived file. Upstream: https://github.com/Open-Cascade-SAS/JT-Assistant
Pinned source: e859e8c2d809f9d9468e6211eba0052f433978ee.

The 104 upstream-derived source/header files permit GPL version 2 or any later version. ModelViewer-Qt distributes the combined work under GPL version 3 using that permission. Fourteen additional decoder source/header files were written for ModelViewer-Qt. Original upstream notices and the GPLv2 text are retained. Dated modification notices, a change summary and a file/hash provenance manifest are in third_party/occt-jt in the corresponding source and licenses/occt-jt in the installed data directory. The upstream viewer, Eigen copy and commercial XT converter are excluded.

This software is provided without warranty; the governing GPL texts contain the full terms. Binary distributions must provide Corresponding Source in a manner allowed by GPLv3 section 6. A project-homepage link by itself is not a substitute for that obligation.

## Siemens JT data-structure notice

JT 10.0 reference data structures and operators: Copyright 2014 Siemens Product Lifecycle Management Software Inc.
JT 10.6 Revision D reference data structures and operators: Copyright 2026 Siemens.

The necessary JT data structures and operators are used under the Siemens JT Data Format Reference Intellectual Property License Terms for software that accepts and interprets JT files. The Siemens reference documents and product code are not redistributed. These statements credit the reference material; they do not attribute ModelViewer-Qt's implementation to Siemens.

Reference and permission sources:
- https://www.siemens.com/en-us/products/plm-components/jt/jt-open-program/
- https://www.plm.automation.siemens.com/en_us/Images/JT-v10-file-format-reference-rev-B_tcm1023-233786.pdf
- https://static.sw.cdn.siemens.com/media/published/4Zc4NsBnmaFlEd0mtssp7f/siemens-jt-v-10-6-file-format-reference-rev-d-10-feb-2026.zip
- https://www.plm.automation.siemens.com/media/global/en/Siemens-PLM-JT-Data-Format-Reference-Patent-Clarification-Notice-mi-X1_tcm27-58013.pdf

The separate patent notice concerns necessary rights under named patents and corresponding foreign patents; it is not blanket patent clearance. The copyright permission has conditions concerning mixed-format files and incorporation of JT structures/schema into other formats. This notice does not broaden those permissions or resolve their application to a particular conversion/container workflow. No Siemens trademark licence, certification, sponsorship or endorsement is claimed.

No Siemens SDK, commercial Parasolid converter or standalone Parasolid importer is included. NVIDIA's downloaded JT validation models are not bundled or licensed for redistribution by these notices.
