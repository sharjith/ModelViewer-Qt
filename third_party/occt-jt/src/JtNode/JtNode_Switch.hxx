// Copyright (C) 2026 ModelViewer-Qt contributors.
// SPDX-License-Identifier: GPL-3.0-only
// Original implementation from the published JT 10.0 reference, section 6.1.1.9.
#pragma once
#include <JtNode_Group.hxx>

class JtNode_Switch : public JtNode_Group
{
public:
  Standard_EXPORT Standard_Boolean Read(JtData_Reader& reader) override;
  Jt_U32 SelectedChild() const { return mySelectedChild; }
  DEFINE_STANDARD_RTTIEXT(JtNode_Switch, JtNode_Group)
  DEFINE_OBJECT_CLASS(JtNode_Switch)
protected:
  Jt_U32 mySelectedChild = 0xffffffffu; // -1 in the published U32 field means none.
};
DEFINE_STANDARD_HANDLE(JtNode_Switch, JtNode_Group)
