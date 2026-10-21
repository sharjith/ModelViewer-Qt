// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original palette-map adapter, JT 10.6 Revision D section 5.4.8.
#pragma once
#include <JtAttribute_Base.hxx>
#include <JtData_Types.hxx>
class JtAttribute_PaletteMap : public JtAttribute_Base {
public:
  Standard_EXPORT Standard_Boolean Read(JtData_Reader& reader) override;
  const Jt_VecI32& Entries() const {return myEntries;}
  DEFINE_STANDARD_RTTIEXT(JtAttribute_PaletteMap,JtAttribute_Base)
  DEFINE_OBJECT_CLASS(JtAttribute_PaletteMap)
protected:
  Jt_VecI32 myEntries;
};
DEFINE_STANDARD_HANDLE(JtAttribute_PaletteMap,JtAttribute_Base)
