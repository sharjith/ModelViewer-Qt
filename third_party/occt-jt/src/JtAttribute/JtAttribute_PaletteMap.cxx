// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original palette-map adapter, JT 10.6 Revision D section 5.4.8 / Annex E.
#include <JtAttribute_PaletteMap.hxx>
#include <JtData_Reader.hxx>
#include <JtDecode_Int32CDP.hxx>
IMPLEMENT_STANDARD_HANDLE(JtAttribute_PaletteMap,JtAttribute_Base)
IMPLEMENT_STANDARD_RTTIEXT(JtAttribute_PaletteMap,JtAttribute_Base)
IMPLEMENT_OBJECT_CLASS(JtAttribute_PaletteMap,"Palette map attribute","10dd1106-2ac8-11d1-9b-6b-00-80-c7-bb-59-97")
Standard_Boolean JtAttribute_PaletteMap::Read(JtData_Reader& reader) {
  Jt_I16 version;
  if(reader.Model()->MajorVersion()<10)throw std::runtime_error("Unsupported JT legacy palette map");
  if(!JtAttribute_Base::Read(reader)||!reader.ReadVersion(version))return Standard_False;
  if(version!=1)throw std::runtime_error("Unsupported JT palette map version");
  JtDecode_Int32CDP packet;
  if(!packet.Load2(reader))return Standard_False;
  myEntries=packet.DecodeI32();
  for(int i=0;i<myEntries.Count();++i)if(myEntries[i]<-2)throw std::runtime_error("Invalid JT palette map entry");
  // Observed bounded 10.9 maps omit their redundant, always-fallback index.
  // Accept only an exact packet boundary, or the published four-byte tail.
  if(BaseVersion()==2&&reader.ElementBytesRemaining()==0)return Standard_True;
  if(BaseVersion()==2&&reader.ElementBytesRemaining()!=4)throw std::runtime_error("Invalid JT palette map trailing fields");
  if(!ReadFieldsV2(reader))return Standard_False;
  if(PaletteIndex()!=-1)throw std::runtime_error("Invalid JT palette map palette index");
  return Standard_True;
}
