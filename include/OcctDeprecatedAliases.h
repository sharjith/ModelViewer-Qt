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
#include <NCollection_DataMap.hxx>
#include <NCollection_IndexedMap.hxx>
#include <NCollection_Sequence.hxx>
#include <TDF_Label.hxx>
#include <TopoDS_Shape.hxx>
#include <TopTools_ShapeMapHasher.hxx>

using TDF_LabelSequence = NCollection_Sequence<TDF_Label>;
using TDF_LabelDataMap = NCollection_DataMap<TDF_Label, TDF_Label>;
using TopTools_IndexedMapOfShape = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;
