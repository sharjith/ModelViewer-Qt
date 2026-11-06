# Importer notice delivery and validation

19 October 2026. This records the earlier documentation/credit-only pass, before the subsequent assembly implementation. Current functional checks are in [JT_ASSEMBLY_ATTRIBUTE_VALIDATION.md](../research/jt/JT_ASSEMBLY_ATTRIBUTE_VALIDATION.md).

- CREDITS.md links the importer notices and consolidated 3DXML provenance.
- About includes importer acknowledgements and a Third-Party Notices button. The popup reads THIRD_PARTY_NOTICES.md from the Qt resource bundle and needs no network access.
- CMake installs CREDITS.md, THIRD_PARTY_NOTICES.md and gpl-3.0.txt beside the application data. The existing Windows installer recursively includes that installed data tree. The source repository retains the detailed engineering provenance.
- The JT subset retains upstream permission and warranty notices, adds dated modification notices, and ships LICENSE.txt, README.md, MODIFICATIONS.md and PROVENANCE.json under licenses/occt-jt. The manifest distinguishes 104 upstream-derived files and five original additions.

## Checks performed

Both branches' actual AboutDialog.cpp and header were compiled in an isolated Qt Widgets harness. The popup button was invoked using Qt's minimal platform plugin; checks verified embedded notice loading and the expected branch-specific credit text. This is a compilation/content check, not a full application build or native-theme visual acceptance test.

The actual notice-related install rules were extracted and run in an isolated CMake installation check. All declared notice/licence files staged successfully. The Windows installer inclusion rule was inspected; a full Windows or Debian installer was not built. Resource XML, notice input paths, branch-specific content and git diff whitespace checks pass.

A source comparison against the branch HEAD, removing only the newly added notice comments, confirms identical contents for all 109 decoder source/header files. Original upstream copyright and GPL-or-later permissions remain present. Importer runtime tests were not repeated during that notice-only pass; they were run for the later assembly/attribute changes.

These checks verify delivery and provenance records. They do not establish patent clearance, resolve every possible format-permission interpretation, or replace GPLv3 Corresponding Source delivery for a binary release. No sample CAD geometry, vendor reference document or proprietary SDK was added.
