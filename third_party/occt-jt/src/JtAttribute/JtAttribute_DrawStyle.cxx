// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original JT 10.0 draw-style reader, reference section 6.1.2.4.
#include <JtAttribute_DrawStyle.hxx>
#include <JtData_Reader.hxx>
#include <stdexcept>

IMPLEMENT_STANDARD_HANDLE(JtAttribute_DrawStyle, JtAttribute_Base)
IMPLEMENT_STANDARD_RTTIEXT(JtAttribute_DrawStyle, JtAttribute_Base)
IMPLEMENT_OBJECT_CLASS(JtAttribute_DrawStyle, "Draw style attribute",
                       "10dd1014-2ac8-11d1-9b-6b-00-80-c7-bb-59-97")

Standard_Boolean JtAttribute_DrawStyle::Read(JtData_Reader& reader)
{
  Jt_I16 version=0;
  if(!JtAttribute_Base::Read(reader) || !reader.ReadVersion(version)) return Standard_False;
  if(version!=1) throw std::runtime_error("Unsupported JT draw style version");
  if(!reader.ReadPrimitiveValue(myDataFlags)) return Standard_False;
  if(myDataFlags & 0xc0) throw std::runtime_error("Unsupported reserved JT draw style flags");
  return Standard_True;
}
