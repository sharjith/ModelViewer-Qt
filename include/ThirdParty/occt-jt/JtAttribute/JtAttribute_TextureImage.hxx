// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original bounded texture descriptor reader; JT 10.0 reference section 6.1.2.3.
#pragma once
#include <JtAttribute_Base.hxx>
#include <array>
#include <vector>

class JtAttribute_TextureImage : public JtAttribute_Base
{
public:
  struct Image {
    Jt_U32 format=0,dataType=0,shared=0;
    Jt_I16 dimensionality=0,alignment=0,width=0,height=0,depth=0,border=0;
    std::vector<Jt_I32> mipBytes;
    std::vector<Jt_U8> baseMip;
    TCollection_ExtendedString externalName;
    Jt_I32 totalBytes=0;
  };
  Standard_EXPORT Standard_Boolean Read(JtData_Reader& reader) override;
  bool Parsed() const {return myParsed;}
  bool Legacy9() const {return myLegacy9;}
  Jt_I16 LocalVersion() const {return myLocalVersion;}
  Jt_I32 TextureType() const {return myType;}
  Jt_I32 Channel() const {return myChannel;}
  Jt_I32 CoordinateChannel() const {return myCoordinateChannel;}
  bool Inline() const {return myInline;}
  const std::vector<Image>& Images() const {return myImages;}
  const std::array<Jt_I32,8>& Environment() const {return myEnvironment;}
  const std::array<Jt_F32,16>& Transform() const {return myTransform;}
  const std::array<Jt_F32,8>& Colors() const {return myColors;}
  const std::array<Jt_I32,4>& Generation() const {return myGeneration;}
  const std::array<Jt_F32,16>& Planes() const {return myPlanes;}
  const TCollection_ExtendedString& OriginFileName() const {return myOrigin;}
  DEFINE_STANDARD_RTTIEXT(JtAttribute_TextureImage, JtAttribute_Base)
  DEFINE_OBJECT_CLASS(JtAttribute_TextureImage)
private:
  bool myParsed=false,myInline=false,myLegacy9=false;
  Jt_I16 myLocalVersion=0;
  Jt_I32 myType=0,myChannel=0,myCoordinateChannel=-1;
  std::array<Jt_I32,8> myEnvironment{};
  std::array<Jt_F32,8> myColors{};
  std::array<Jt_F32,16> myTransform{},myPlanes{};
  std::array<Jt_I32,4> myGeneration{};
  std::vector<Image> myImages;
  TCollection_ExtendedString myOrigin;
};
DEFINE_STANDARD_HANDLE(JtAttribute_TextureImage, JtAttribute_Base)
