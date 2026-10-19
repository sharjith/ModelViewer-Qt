// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original JT 10.0 scalar property readers, reference sections 6.2.3 and 6.2.4.
#include <JtProperty_Scalar.hxx>
#include <JtData_Reader.hxx>
#include <stdexcept>

IMPLEMENT_STANDARD_HANDLE(JtProperty_Integer, JtProperty_Base)
IMPLEMENT_STANDARD_RTTIEXT(JtProperty_Integer, JtProperty_Base)
IMPLEMENT_OBJECT_CLASS(JtProperty_Integer, "Integer property",
                       "10dd102b-2ac8-11d1-9b-6b-00-80-c7-bb-59-97")
IMPLEMENT_STANDARD_HANDLE(JtProperty_Float, JtProperty_Base)
IMPLEMENT_STANDARD_RTTIEXT(JtProperty_Float, JtProperty_Base)
IMPLEMENT_OBJECT_CLASS(JtProperty_Float, "Floating point property",
                       "10dd1019-2ac8-11d1-9b-6b-00-80-c7-bb-59-97")

Standard_Boolean JtProperty_Integer::Read(JtData_Reader& reader)
{
  Jt_I16 version=0;
  if(!JtProperty_Base::Read(reader) || !reader.ReadVersion(version)) return Standard_False;
  if(version!=1) throw std::runtime_error("Unsupported JT integer property version");
  return reader.ReadPrimitiveValue(myValue);
}

Standard_Boolean JtProperty_Float::Read(JtData_Reader& reader)
{
  Jt_I16 version=0;
  if(!JtProperty_Base::Read(reader) || !reader.ReadVersion(version)) return Standard_False;
  if(version!=1) throw std::runtime_error("Unsupported JT floating point property version");
  return reader.ReadPrimitiveValue(myValue);
}
