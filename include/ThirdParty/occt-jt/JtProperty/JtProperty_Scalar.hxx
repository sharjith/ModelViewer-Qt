// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original JT 10.0 scalar property readers, reference sections 6.2.3 and 6.2.4.
#pragma once
#include <JtProperty_Base.hxx>

class JtProperty_Integer : public JtProperty_Base
{
public:
  Standard_EXPORT Standard_Boolean Read(JtData_Reader& reader) override;
  Jt_I32 Value() const { return myValue; }
  DEFINE_STANDARD_RTTIEXT(JtProperty_Integer, JtProperty_Base)
  DEFINE_OBJECT_CLASS(JtProperty_Integer)
protected:
  Jt_I32 myValue=0;
};
DEFINE_STANDARD_HANDLE(JtProperty_Integer, JtProperty_Base)

class JtProperty_Float : public JtProperty_Base
{
public:
  Standard_EXPORT Standard_Boolean Read(JtData_Reader& reader) override;
  Jt_F32 Value() const { return myValue; }
  DEFINE_STANDARD_RTTIEXT(JtProperty_Float, JtProperty_Base)
  DEFINE_OBJECT_CLASS(JtProperty_Float)
protected:
  Jt_F32 myValue=0;
};
DEFINE_STANDARD_HANDLE(JtProperty_Float, JtProperty_Base)
