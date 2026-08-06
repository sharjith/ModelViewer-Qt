#pragma once

// OCCT 8.0.0 deprecated several old short-name typedef headers
// (TDF_LabelSequence.hxx, TDF_LabelDataMap.hxx, TopTools_IndexedMapOfShape.hxx)
// in favor of spelling out the underlying NCollection template directly at
// every use site. This codebase uses those old short names in a number of
// places (XCAFDocProcessor, XCAFSTEPProcessor, XCAFIGESProcessor,
// BRepToAssimpConverter), so rather than rewriting every declaration/call
// site to the longer template form, this header re-declares the exact same
// aliases OCCT itself used to provide - include this instead of the
// deprecated headers, and every existing use site keeps compiling unchanged.
//
// On OCCT < 8.0 (e.g. the apt-packaged 7.9.x builds this project also
// targets for the .deb), those old headers aren't deprecated at all - they
// still declare these exact same names natively, so re-declaring them here
// too would just be a redundant (at best) or conflicting (at worst,
// depending on how that OCCT version spells the underlying template)
// declaration. Include OCCT's own headers directly instead in that case.
#include <Standard_Version.hxx>

#if OCC_VERSION_MAJOR >= 8
#include <NCollection_DataMap.hxx>
#include <NCollection_IndexedMap.hxx>
#include <NCollection_Sequence.hxx>
#include <TDF_Label.hxx>
#include <TopoDS_Shape.hxx>
#include <TopTools_ShapeMapHasher.hxx>

using TDF_LabelSequence = NCollection_Sequence<TDF_Label>;
using TDF_LabelDataMap = NCollection_DataMap<TDF_Label, TDF_Label>;
using TopTools_IndexedMapOfShape = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;
#else
#include <TDF_LabelSequence.hxx>
#include <TDF_LabelDataMap.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#endif
