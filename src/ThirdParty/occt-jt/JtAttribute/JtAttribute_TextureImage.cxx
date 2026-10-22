// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original bounded texture reader; JT 10.0 section 6.1.2.3 and JT 9.5 section 7.2.1.1.2.3.
// JT 9.x local-version-1/2/3 hierarchy added 2026-10-22.
#include <JtAttribute_TextureImage.hxx>
#include <JtData_Reader.hxx>
#include <JtData_Limits.hxx>
#include <cmath>
#include <algorithm>
#include <stdexcept>

IMPLEMENT_STANDARD_HANDLE(JtAttribute_TextureImage, JtAttribute_Base)
IMPLEMENT_STANDARD_RTTIEXT(JtAttribute_TextureImage, JtAttribute_Base)
IMPLEMENT_OBJECT_CLASS(JtAttribute_TextureImage, "Texture image attribute",
                       "10dd1073-2ac8-11d1-9b-6b-00-80-c7-bb-59-97")

Standard_Boolean JtAttribute_TextureImage::Read(JtData_Reader& reader)
{
  myParsed=false;myImages.clear();myLocalVersion=0;myLegacy9=reader.Model()->MajorVersion()==9;
  myOrigin=reader.Model()->FileName();
  myImageDataLE=reader.Model()->IsFileLE(); // raw texel bytes are retained without host swapping
  if(!JtAttribute_Base::Read(reader))return Standard_False;
  // JT 8.x still needs applicable layout evidence. JT 9.x uses a distinct
  // local-version hierarchy, with complete lower-version compatibility stubs.
  if(reader.Model()->MajorVersion()<9)return Standard_True;
  Jt_I16 version;Jt_U32 empty;Jt_U8 storage;Jt_I32 count;
  if(!reader.ReadVersion(version))return Standard_False;
  myLocalVersion=version;
  if(myLegacy9&&version>3)return Standard_True; // future layouts remain envelope-bounded metadata
  if(version<1||(!myLegacy9&&version!=1))throw std::runtime_error("Unsupported JT texture image version");
  const int blocks=myLegacy9?version:1;
  uint64_t total=0;
  for(int block=1;block<=blocks;++block){
    JtData_Limits::poll();
    if(!reader.ReadI32(myType))return Standard_False;
    const int maxType=myLegacy9?(block==1?6:block==2?25:26):26;
    if(myType<0||myType>maxType||(myLegacy9&&block>1&&myType==6))throw std::runtime_error("Invalid JT texture type");
    for(auto& v:myEnvironment)if(!reader.ReadI32(v))return Standard_False;
    const int limits[8]={2,2,6,5,5,5,6,3};
    for(size_t i=0;i<myEnvironment.size();++i)
      if(myEnvironment[i]<0||myEnvironment[i]>limits[i])throw std::runtime_error("Invalid JT texture environment");
    const auto floats=[&](auto& values){for(auto& v:values){if(!reader.ReadF32(v))return false;if(!std::isfinite(v))throw std::runtime_error("Nonfinite JT texture parameter");}return true;};
    if(!floats(myColors)||!floats(myTransform))return Standard_False;
    for(auto& v:myGeneration){if(!reader.ReadI32(v))return Standard_False;if(v<0||v>5)throw std::runtime_error("Invalid JT texture coordinate generation");}
    if(!floats(myPlanes)||!reader.ReadI32(myChannel))return Standard_False;
    // V9 blocks 1/2 default to UV0. Block 3 appends its coordinate channel
    // AFTER the images (figure 50), unlike the modern prefix field.
    myCoordinateChannel=0;
    if(!myLegacy9&&!reader.ReadI32(myCoordinateChannel))return Standard_False;
    if(!reader.ReadU32(empty)||!reader.ReadU8(storage)||!reader.ReadI32(count))return Standard_False;
    if(myChannel<-1||myCoordinateChannel<-1)throw std::runtime_error("Invalid JT texture coordinate/channel");
    if(myLegacy9&&block<3&&(myChannel<(block==1?0:-1)||myChannel>31))throw std::runtime_error("Invalid JT 9.x texture channel");
    if(storage>1||count<0||count>64)throw std::runtime_error("Invalid JT texture image storage/count");
    if(block<blocks&&(myType!=0||count!=0))throw std::runtime_error("Invalid JT 9.x texture compatibility stub");
    myInline=storage==1;
    JtData_Limits::allocation(size_t(count)*sizeof(Image));myImages.resize(size_t(count));
    for(auto& image:myImages){
      JtData_Limits::poll();
      if(!myInline){if(!reader.ReadMbString(image.externalName))return Standard_False;JtData_Limits::allocation(size_t(image.externalName.Length())*4);continue;}
      Jt_I16 mipCount;
      if(!reader.ReadU32(image.format)||!reader.ReadU32(image.dataType)
        ||!reader.ReadI16(image.dimensionality)||!reader.ReadI16(image.alignment)
        ||!reader.ReadI16(image.width)||!reader.ReadI16(image.height)||!reader.ReadI16(image.depth)
        ||!reader.ReadI16(image.border))return Standard_False;
      if(myLegacy9){Jt_U8 shared;if(!reader.ReadU8(shared))return Standard_False;image.shared=shared;}
      else if(!reader.ReadU32(image.shared))return Standard_False;
      if(!reader.ReadI16(mipCount)||!reader.ReadI32(image.totalBytes))return Standard_False;
      if(image.format>13||image.dataType>9||image.dimensionality<1||image.dimensionality>3
        ||(image.alignment!=1&&image.alignment!=2&&image.alignment!=4&&image.alignment!=8)
        ||image.width<1||image.height<1||image.depth<1||image.border<0||image.border>1
        ||image.shared>1||mipCount<1||mipCount>16||image.totalBytes<0)
        throw std::runtime_error("Invalid JT inline texture descriptor");
      total+=unsigned(image.totalBytes);
      if(total>64u*1024u*1024u)throw std::runtime_error("JT texture image byte limit exceeded");
      JtData_Limits::allocation(size_t(mipCount)*sizeof(Jt_I32));image.mipBytes.resize(size_t(mipCount));
      uint64_t consumed=0;
      for(size_t level=0;level<image.mipBytes.size();++level){
        auto& bytes=image.mipBytes[level];
        if(!reader.ReadI32(bytes))return Standard_False;
        if(bytes<0||(consumed+=unsigned(bytes))>unsigned(image.totalBytes))throw std::runtime_error("Invalid JT texture mip byte count");
        // Retain the base image for supported material mapping. Higher authored
        // mip levels remain bounded skips; the renderer regenerates its chain.
        if(level==0){JtData_Limits::allocation(size_t(bytes));image.baseMip.resize(size_t(bytes));}
        for(size_t done=0;done<size_t(bytes);){
          JtData_Limits::poll();const size_t n=std::min(size_t(bytes)-done,size_t(65536));
          if(level==0 ? !reader.ReadBytes(image.baseMip.data()+done,n) : !reader.SkipBytes(n))return Standard_False;
          done+=n;
        }
      }
      if(consumed!=unsigned(image.totalBytes))throw std::runtime_error("JT texture total byte count mismatch");
    }
    if(myLegacy9&&block==3){
      if(!reader.ReadI32(myCoordinateChannel))return Standard_False;
      if(myCoordinateChannel<-1)throw std::runtime_error("Invalid JT texture coordinate channel");
    }
  }
  if(!ReadFieldsV2(reader))return Standard_False;
  myParsed=true;return Standard_True;
}
