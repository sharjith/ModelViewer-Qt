// Copyright (C) 2026 ModelViewer-Qt contributors.
// SPDX-License-Identifier: GPL-3.0-only
// Original implementation from the published JT 10.0 reference, section 6.1.1.9.
#include <JtNode_Switch.hxx>
#include <JtData_Reader.hxx>
#include <stdexcept>

IMPLEMENT_STANDARD_HANDLE(JtNode_Switch, JtNode_Group)
IMPLEMENT_STANDARD_RTTIEXT(JtNode_Switch, JtNode_Group)
IMPLEMENT_OBJECT_CLASS(JtNode_Switch, "Switch Object",
                       "10dd10f3-2ac8-11d1-9b-6b-00-80-c7-bb-59-97")

Standard_Boolean JtNode_Switch::Read(JtData_Reader& reader)
{
  if(reader.Model()->MajorVersion()<9) throw std::runtime_error("Unsupported JT 8.x Switch node layout");
  Jt_I16 version = 0;
  if (!JtNode_Group::Read(reader) || !reader.ReadVersion(version)) return Standard_False;
  if (version != 1) throw std::runtime_error("Unsupported JT Switch node version");
  if (!reader.ReadPrimitiveValue(mySelectedChild)) return Standard_False;
  if (mySelectedChild != 0xffffffffu && mySelectedChild >= Children().Count())
    throw std::runtime_error("JT Switch selected child is out of range");
  return Standard_True;
}
