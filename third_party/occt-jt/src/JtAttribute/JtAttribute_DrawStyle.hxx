// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original JT 10.0 draw-style reader, reference section 6.1.2.4.
#pragma once
#include <JtAttribute_Base.hxx>

class JtAttribute_DrawStyle : public JtAttribute_Base
{
public:
  Standard_EXPORT Standard_Boolean Read(JtData_Reader& reader) override;
  Jt_U8 DataFlags() const { return myDataFlags; }
  DEFINE_STANDARD_RTTIEXT(JtAttribute_DrawStyle, JtAttribute_Base)
  DEFINE_OBJECT_CLASS(JtAttribute_DrawStyle)
protected:
  Jt_U8 myDataFlags=0x0a;
};
DEFINE_STANDARD_HANDLE(JtAttribute_DrawStyle, JtAttribute_Base)
