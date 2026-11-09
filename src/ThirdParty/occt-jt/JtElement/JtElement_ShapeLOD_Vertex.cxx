// JT format reading and visualization tools
// Copyright (C) 2013-2015 OPEN CASCADE SAS
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2 of the License, or any later
// version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// Copy of the GNU General Public License is in LICENSE.txt and
// on <http://www.gnu.org/licenses/>.

// Modified for ModelViewer-Qt, 2026-10-19 (including changes made 2026-10-18).
// Additional bounded palette/face-group qualification changes: 2026-10-20.
// Original topological UV transport and corner-record preservation: 2026-10-21.
// Four-set UV retention: 2026-10-21.
// Changes: modern OCCT compatibility, JT 10.0 decoding and bounded input handling;
// see src/ThirdParty/occt-jt/MODIFICATIONS.md and PROVENANCE.json for provenance.

#include <JtElement_ShapeLOD_Vertex.hxx>

#include <NCollection_Handle.hxx>

#include <JtData_Reader.hxx>
#include <JtData_Inflate.hxx>
#include <JtData_SkippedContent.hxx>

#include <JtDecode_Int32CDP.hxx>
#include <JtDecode_VertexData.hxx>
#include <JtDecode_VertexData_Quantized.hxx>
#include <JtDecode_VertexColors.hxx>
#include <JtDecode_VertexUV.hxx>
#include <JtDecode_MeshCoderDriver.hxx>

#include <map>
#include <vector>
#include <memory>
#include <cmath>

//#define NO_JT_MULTITHREADING
#include <JtData_Parallel.hxx>

IMPLEMENT_STANDARD_HANDLE (JtElement_ShapeLOD_Vertex, JtElement_ShapeLOD_Base)
IMPLEMENT_STANDARD_RTTIEXT(JtElement_ShapeLOD_Vertex, JtElement_ShapeLOD_Base)

IMPLEMENT_OBJECT_CLASS(JtElement_ShapeLOD_Vertex, "Vertex Shape LOD Object",
                       "10dd10b0-2ac8-11d1-6b-9b-00-80-c7-bb-59-97")

class JtElement_ShapeLOD_Vertex::VertexDataDecodeTask
{
public:

  VertexDataDecodeTask (JtData_SingleHandle<JtDecode_VertexData> theData, VertexData& theResult)
    : myData (theData) , myResultPtr (&theResult) {}

  void operator ()() const { *myResultPtr = const_cast <JtDecode_VertexData&> (*myData).Decode(); }

private:

  NCollection_Handle<JtDecode_VertexData> myData;
  VertexData* myResultPtr;
};

class JtElement_ShapeLOD_Vertex::MeshDecodeTask
{
  NCollection_Handle<JtDecode_MeshCoderDriver::InputData> myData;
  IndicesVec* myVertexIndicesPtr;
  IndicesVec* myNormalIndicesPtr;
  IndicesVec* myFaceGroupsPtr;

public:
  MeshDecodeTask (JtData_SingleHandle<JtDecode_MeshCoderDriver::InputData> theData,
                  IndicesVec& theVertexIndices,
                  IndicesVec& theNormalIndices, IndicesVec& theFaceGroups)
    : myData             (theData)
    , myVertexIndicesPtr (&theVertexIndices)
    , myNormalIndicesPtr (&theNormalIndices)
    , myFaceGroupsPtr (&theFaceGroups) {}

  void operator ()() const
  {
    JtDecode_MeshCoderDriver aMeshCoderDriver;
    aMeshCoderDriver.SetInputData (const_cast <JtDecode_MeshCoderDriver::InputData&> (*myData));
    aMeshCoderDriver.Decode       (myVertexIndicesPtr, myNormalIndicesPtr, myFaceGroupsPtr);
  }
};

//=======================================================================
//function : QuantizationParams::Read
//purpose  :
//=======================================================================
Standard_Boolean JtElement_ShapeLOD_Vertex::QuantizationParams::Read (JtData_Reader& theReader)
{
  return theReader.ReadUniformStruct<Jt_U8> (*this);
}

//=======================================================================
//function : VertexBinding1::Read
//purpose  :
//=======================================================================
Standard_Boolean JtElement_ShapeLOD_Vertex::VertexBinding1::Read (JtData_Reader& theReader)
{
  uint8_t aBuffer[sizeof (Jt_I32)];
  if (!theReader.ReadArray (aBuffer))
    return Standard_False;

  if (theReader.Model()->IsFileLE())
  {
    myNormalBinding    = aBuffer[0];
    myTextCoordBinding = aBuffer[1];
    myColorBinding     = aBuffer[2];
  }
  else
  {
    myNormalBinding    = aBuffer[3];
    myTextCoordBinding = aBuffer[2];
    myColorBinding     = aBuffer[1];
  }

  return Standard_True;
}

//=======================================================================
//function : VertexBinding2::Read
//purpose  :
//=======================================================================
Standard_Boolean JtElement_ShapeLOD_Vertex::VertexBinding2::Read (JtData_Reader& theReader)
{
  if (theReader.Model()->IsFileLE())
  {
    return theReader.ReadU8  (reinterpret_cast <uint8_t&> (myLSB))
        && theReader.ReadU32 (myTextFlags)
        && theReader.Skip    (sizeof (Jt_U64) - sizeof (uint32_t) - sizeof (uint8_t) * 2)
        && theReader.ReadU8  (reinterpret_cast <uint8_t&> (myMSB));
  }
  else
  {
    return theReader.ReadU8  (reinterpret_cast <uint8_t&> (myMSB))
        && theReader.Skip    (sizeof (Jt_U64) - sizeof (uint32_t) - sizeof (uint8_t) * 2)
        && theReader.ReadU32 (myTextFlags)
        && theReader.ReadU8  (reinterpret_cast <uint8_t&> (myLSB));
  }
}

//=======================================================================
//function : VertexBinding2::IsNormalBinding
//purpose  :
//=======================================================================
Standard_Boolean JtElement_ShapeLOD_Vertex::VertexBinding2::IsNormalBinding()
{
  return myLSB.Normal;
}

//=======================================================================
//function : VertexBinding2::IsFlagBinding
//purpose  :
//=======================================================================
Standard_Boolean JtElement_ShapeLOD_Vertex::VertexBinding2::IsFlagBinding()
{
  return myLSB.Flag;
}

//=======================================================================
//function : VertexBinding2::IsAuxFieldBinding
//purpose  :
//=======================================================================
Standard_Boolean JtElement_ShapeLOD_Vertex::VertexBinding2::IsAuxFieldBinding()
{
  return myMSB.Auxiliary;
}

//=======================================================================
//function : VertexBinding2::NbVertexCoordComponents
//purpose  :
//=======================================================================
Standard_Size JtElement_ShapeLOD_Vertex::VertexBinding2::NbVertexCoordComponents()
{
  switch (myLSB.Vertex)
  {
  case 1 << 0: return 2;
  case 1 << 1: return 3;
  case 1 << 2: return 4;
  default: return 0;
  }
}

//=======================================================================
//function : VertexBinding2::NbColorComponents
//purpose  :
//=======================================================================
Standard_Size JtElement_ShapeLOD_Vertex::VertexBinding2::NbColorComponents()
{
  switch (myLSB.Color)
  {
  case 1 << 0: return 3;
  case 1 << 1: return 4;
  default:
    if(myLSB.Color) throw std::runtime_error("Invalid JT vertex colour binding");
    return 0;
  }
}

//=======================================================================
//function : VertexBinding2::NbTextCoordComponents
//purpose  :
//=======================================================================
Standard_Size JtElement_ShapeLOD_Vertex::VertexBinding2::NbTextCoordComponents
  (Standard_Size theTexture)
{
  if(theTexture>=8)throw std::runtime_error("Invalid JT texture-coordinate channel index");
  const unsigned binding=(myTextFlags >> (theTexture * 4)) & 0x0F;
  switch (binding)
  {
  case 1 << 0: return 1;
  case 1 << 1: return 2;
  case 1 << 2: return 3;
  case 1 << 3: return 4;
  default:
    if(binding)throw std::runtime_error("Invalid JT texture-coordinate binding");
    return 0;
  }
}

unsigned JtElement_ShapeLOD_Vertex::VertexBinding2::TextureChannelCount() {
  unsigned count=0;
  for(unsigned channel=0;channel<8;++channel){
    const auto components=NbTextCoordComponents(channel);
    if(!components)continue;
    if(channel>=MaxTextureChannels || (components!=1 && components!=2))
      throw std::runtime_error("Unsupported JT texture-coordinate layout (qualified: scalar/UV channels 0-3)");
    ++count;
  }
  return count;
}

//=======================================================================
//function : Read
//purpose  : Read this entity from a translate file
//=======================================================================
Standard_Boolean JtElement_ShapeLOD_Vertex::Read (JtData_Reader& theReader)
{
  return readVertexShapeLODData (theReader);
}

//=======================================================================
//function : Dump
//purpose  :
//=======================================================================
Standard_Integer JtElement_ShapeLOD_Vertex::Dump (Standard_OStream& theStream) const
{
  theStream << "JtElement_ShapeLOD_Vertex ";
  return JtElement_ShapeLOD_Base::Dump (theStream);
}

//=======================================================================
//function : readVertexShapeLODData
//purpose  : Read Vertex Shape LOD Data collection
//=======================================================================
Standard_Boolean JtElement_ShapeLOD_Vertex::readVertexShapeLODData (
  JtData_Reader&   theReader,
  Standard_Boolean theIsTriStripSet,
  PrimitiveKind    theKind)
{
  myExplicitEmpty=false;
  myPrimitive=theKind;
  myIndices.Free();myFaceGroups.Free();
  myVertices.Free();
  myNormals.Free();
  myColors.Free();
  for(auto& uv:myTexCoords)uv.Free();

  if (theReader.Model()->MajorVersion() < 9)
  {
    if (!JtData_Object::Read (theReader))
      return Standard_False;

    if (theReader.Model()->MajorVersion() > 7)
    {
      Jt_I16 aVersion;
      if (!theReader.ReadVersion(aVersion))
        return Standard_False;
      if(aVersion!=1) throw std::runtime_error("Unsupported JT 8.x vertex LOD version");
    }

    VertexBinding1     aVertexBinding;
    QuantizationParams aQuantizationParams;
    return aVertexBinding     .Read (theReader)
        && aQuantizationParams.Read (theReader);
  }
  else
  {
    Jt_I16 aVersion;
    VertexBinding2 aVertexBinding;
    Jt_I16 aMeshVersion;
    Jt_I32 aTriID;
    Jt_I16 aVersion4;
    if (!JtElement_ShapeLOD_Base::Read (theReader)
     || !theReader.ReadVersion(aVersion)
     || !aVertexBinding.Read (theReader)) return Standard_False;
    if(theReader.Model()->MajorVersion()>=10) {
      Jt_I32 length, id; Jt_GUID guid; Jt_U8 base;
      if(!theReader.ReadI32(length) || length<21 || !theReader.ReadGUID(guid)
        || guid != (theIsTriStripSet || theKind==PrimitiveKind::Triangles ? Jt_GUID("f830a5ad-be4c-4fbc-9b-5f-b9-26-92-78-d2-e1")
                    // Observed in NX-written JT 10.10 polyline / point set LODs (C3 samples); the two differ in the last byte.
                    : theKind==PrimitiveKind::Lines ? Jt_GUID("11c12d32-38f9-45ba-93-ba-66-f9-d5-38-dd-fb")
                                                    : Jt_GUID("11c12d32-38f9-45ba-93-ba-66-f9-d5-38-dd-fc"))
        || !theReader.ReadU8(base) || base!=9 || !theReader.ReadI32(id)) return Standard_False;
    }
    if (
        !theReader.ReadVersion(aMeshVersion)
     || !theReader.ReadI32 (aTriID)
     || !theReader.ReadVersion(aVersion4))
    {
      return Standard_False;
    }

    if (theIsTriStripSet)
      return readTopologicallyCompressedData (theReader);
    else if (theKind != PrimitiveKind::Triangles)
    {
      if (theReader.Model()->MajorVersion() < 10)
      {
        JtData_SkippedContent::record("polyline or point set LOD in a JT 9.x file (not decoded)");
        return Standard_True;
      }
      return readTopoMeshCompressedRepData (theReader, theKind);
    }
    else
    {
      // TODO: Read TopoMesh Compressed Rep Data
      // Not decoded: the shape carries no geometry here. Counted so the importer reports it instead of dropping it silently.
      JtData_SkippedContent::record("vertex shape LOD with TopoMesh compressed representation data (not a tri-strip set)");
      return Standard_True;
    }
  }
}

//=======================================================================
//function : readVertexBasedShapeCompressedRepData
//purpose  : Read and decode Vertex Based Shape Compressed Rep Data collection
//=======================================================================
namespace {
// Original ModelViewer bounded legacy attribute adapters. No texture image or
// shader interpretation is implied by retaining the source UV coordinates.
using LegacyVertexData=JtElement_ShapeLOD_Vertex::VertexData;
using LegacyQuantizer=JtDecode_VertexData_Quantized::UniformQuantizerData;
Jt_VecI32::Mover legacyCodes(JtData_Reader& reader,int count,JtDecode_Unpack& predictor) {
  JtDecode_Int32CDP packet;
  if(!packet.Load1(reader) || packet.GetOutValCount()!=count)
    throw std::runtime_error("Legacy JT attribute packet count mismatch");
  return packet.DecodeI32(predictor);
}
LegacyVertexData::Mover legacyUV(JtData_Reader& reader,int count) {
  LegacyQuantizer quantizers[2];Jt_U8 suggested;
  if(!quantizers[0].Read(reader) || !quantizers[1].Read(reader) || !reader.ReadU8(suggested))
    throw std::runtime_error("Truncated legacy JT texture quantizer");
  if(suggested>24 || quantizers[0].bits>24 || quantizers[1].bits>24)
    throw std::runtime_error("Unsupported legacy JT texture quantization");
  LegacyVertexData output(count,2);
  for(unsigned component=0;component<2;++component) {
    Jt_VecI32 codes(legacyCodes(reader,count,JtDecode_Unpack_Lag1));const auto& q=quantizers[component];
    if(!q.bits && q.min!=q.max)throw std::runtime_error("Unsupported legacy JT unquantized UV component");
    const double levels=std::ldexp(1.0,q.bits),step=(double(q.max)-q.min)/levels;
    for(int i=0;i<count;++i) {
      if((i&1023)==0) JtData_Limits::poll();
      if(double(uint32_t(codes[i]))>levels) throw std::runtime_error("Legacy JT UV code out of range");
      output[i][component]=float(q.min+(double(uint32_t(codes[i]))-0.5)*step);
      if(!std::isfinite(output[i][component])) throw std::runtime_error("Nonfinite legacy JT UV");
    }
  }
  return output.Move();
}
LegacyVertexData::Mover legacyConstantColors(JtData_Reader& reader,int count) {
  Jt_U8 space,bits,components,arrays;LegacyQuantizer q[4];
  if(!reader.ReadU8(space)) throw std::runtime_error("Truncated legacy JT colour quantizer");
  if(space!=0) throw std::runtime_error("Unsupported legacy JT HSV colour quantizer");
  for(auto& quantizer:q) if(!quantizer.Read(reader)) throw std::runtime_error("Truncated legacy JT colour range");
  if(!reader.ReadU8(bits) || !reader.ReadU8(components) || !reader.ReadU8(arrays))
    throw std::runtime_error("Truncated legacy JT colour array header");
  // This stage qualifies the packed, constant RGB(A) form observed in the
  // corpus. Variable packed/component-array and HSV colours remain explicit.
  if(bits>24 || (components!=3 && components!=4) || arrays!=0)
    throw std::runtime_error("Unsupported legacy JT quantized colour array layout");
  unsigned packedBits=0;
  for(unsigned c=0;c<4;++c) {
    if(q[c].bits>24 || q[c].min!=q[c].max || q[c].min<0 || q[c].max>1)
      throw std::runtime_error("Unsupported nonconstant legacy JT quantized colours");
    if(c<components)packedBits+=q[c].bits;
  }
  if(packedBits>32) throw std::runtime_error("Unsupported legacy JT packed colour width");
  Jt_VecI32 codes(legacyCodes(reader,count,JtDecode_Unpack_Lag1));LegacyVertexData output(count,components);
  for(int i=0;i<count;++i) {
    if((i&1023)==0) JtData_Limits::poll();
    if(packedBits<32 && (uint32_t(codes[i])>>packedBits)!=0)
      throw std::runtime_error("Legacy JT packed colour code out of range");
    for(unsigned c=0;c<components;++c) output[i][c]=q[c].min;
  }
  return output.Move();
}
}
Standard_Boolean JtElement_ShapeLOD_Vertex::readVertexBasedShapeCompressedRepData (JtData_Reader& reader)
{
  // Shared JT 8.x lossless/qualified quantized paths. Bindings and encodings
  // are checked independently of the accepted file-version family.
  Jt_I16 version;Jt_U8 normalBinding,uvBinding,colorBinding;QuantizationParams quant;
  if(!reader.ReadVersion(version) || !reader.ReadU8(normalBinding)
     || !reader.ReadU8(uvBinding) || !reader.ReadU8(colorBinding) || !quant.Read(reader))
    return Standard_False;
  if(version!=1) throw std::runtime_error("Unsupported JT 8.x raw vertex version");
  if(normalBinding>1 || uvBinding>1 || colorBinding>1)
    throw std::runtime_error("Unsupported JT 8.x per-facet/per-primitive attribute binding");
  if(quant.BitsPerVertex()>24) throw std::runtime_error("Unsupported JT 8.x position quantization width");
  if(!quant.BitsPerVertex() && (uvBinding || colorBinding))
    throw std::runtime_error("Unsupported JT 8.x lossless raw vertex attribute binding");
  JtDecode_Int32CDP packet;
  if(!packet.Load1(reader)) return Standard_False;
  Jt_VecI32 strips(packet.DecodeI32(JtDecode_Unpack_Stride1));
  if(strips.Count()<2 || strips.First()!=0 || strips.Last()<3 || strips.Last()>16000000)
    throw std::runtime_error("Invalid JT 8.x strip boundaries");
  size_t triangles=0;
  for(int i=0;i<strips.Count()-1;++i) {
    if(strips[i]<0 || strips[i+1]<strips[i] || strips[i+1]>strips.Last() || strips[i+1]-strips[i]<3)
      throw std::runtime_error("Invalid JT 8.x strip range");
    triangles+=size_t(strips[i+1]-strips[i]-2);
    if(triangles>16000000) throw std::runtime_error("JT 8.x triangle limit exceeded");
  }
  if(quant.BitsPerVertex()) {
    auto coords=JtDecode_VertexData::LoadQuantizedCoords(reader);
    if(!coords)throw std::runtime_error("Cannot decode legacy JT quantized positions");
    const int count=coords->GetOutVertexCount();
    myVertices=coords->Decode();
    if(normalBinding) {
      auto normals=JtDecode_VertexData::LoadQuantizedNormals(reader);
      if(!normals || normals->GetOutVertexCount()!=count)throw std::runtime_error("Legacy JT quantized normal count mismatch");
      myNormals=normals->Decode();
    }
    if(uvBinding)myTexCoords[0]=legacyUV(reader,count);
    if(colorBinding)myColors=legacyConstantColors(reader,count);
    Jt_VecI32 vertices(legacyCodes(reader,strips.Last(),JtDecode_Unpack_StripIdx));
    for(int i=0;i<vertices.Count();++i)
      if(vertices[i]<0 || vertices[i]>=count)throw std::runtime_error("Legacy JT quantized vertex index out of range");
    myIndices.Allocate(int(triangles*3));int output=0;
    for(int strip=0;strip<strips.Count()-1;++strip) {
      JtData_Limits::poll();
      for(int j=0;j<strips[strip+1]-strips[strip]-2;++j) {
        const int a=strips[strip]+j;myIndices[output++]=vertices[a];
        myIndices[output++]=vertices[a+(j%2?2:1)];myIndices[output++]=vertices[a+(j%2?1:2)];
      }
    }
    return Standard_True;
  }
  Jt_I32 uncompressedSize,compressedSize;
  if(!reader.ReadI32(uncompressedSize) || !reader.ReadI32(compressedSize)) return Standard_False;
  const size_t expected=size_t(strips.Last())*(normalBinding?6:3)*sizeof(float);
  if(uncompressedSize<0 || size_t(uncompressedSize)!=expected || expected>256u*1024u*1024u
     || compressedSize>256*1024*1024 || (compressedSize<0 && compressedSize!=-uncompressedSize))
    throw std::runtime_error("Invalid JT 8.x raw vertex byte count");
  std::unique_ptr<JtData_Inflate> inflated;
  JtData_Reader* data=&reader;
  if(compressedSize>0) {inflated=std::make_unique<JtData_Inflate>(reader,size_t(compressedSize));data=inflated.get();}
  const int count=strips.Last();myVertices.Allocate(count,3);
  if(normalBinding) myNormals.Allocate(count,3);
  for(int i=0;i<count;++i) {
    JtData_Limits::poll();
    if((normalBinding && !data->ReadArray(myNormals[i])) || !data->ReadArray(myVertices[i]))
      throw std::runtime_error("Truncated JT 8.x raw vertex array");
  }
  if(inflated && !inflated->Finish(true)) throw std::runtime_error("Invalid/trailing JT 8.x vertex zlib data");
  myIndices.Allocate(int(triangles*3));int output=0;
  for(int strip=0;strip<strips.Count()-1;++strip) {
    // Winding restarts at every strip, including a sequence of 3-vertex strips.
    for(int j=0;j<strips[strip+1]-strips[strip]-2;++j) {
      const int a=strips[strip]+j;myIndices[output++]=a;
      myIndices[output++]=a+(j%2?2:1);myIndices[output++]=a+(j%2?1:2);
    }
  }
  return Standard_True;
}
//=======================================================================
//function : readTopologicallyCompessedData
//purpose  : Read and decode Topologically Compressed Rep Data collection
//=======================================================================
Standard_Boolean JtElement_ShapeLOD_Vertex::readTopologicallyCompressedData (JtData_Reader& theReader)
{
  // Create a TBB task group for parallel decoding of read data
  JtData_Parallel::TaskGroup aDecodeTasks;

  // Read mesh data and start decoding it
  JtDecode_MeshCoderDriver::InputData::Handle aMeshData =
    JtDecode_MeshCoderDriver::LoadInputData (theReader);

  if (!aMeshData)
    return Standard_False;

  IndicesVec aVertexIndices, aNormalIndices;
  bool emptyTopology=true;
  for(unsigned i=0;i<8;++i)emptyTopology=emptyTopology && aMeshData->_viOutDegSyms[i].GetOutValCount()==0 && aMeshData->_vvuOutAttrMasks[i].GetOutValCount()==0;
  for(const auto* packet:{&aMeshData->_vviOutValSyms,&aMeshData->_viOutFGrpSyms,&aMeshData->_vuOutFaceFlags,&aMeshData->_faceAttributeMask8_30,&aMeshData->_vuOutAttrMasksLrg,&aMeshData->_viOutSplitVtxSyms,&aMeshData->_viOutSplitPosSyms})
    emptyTopology=emptyTopology && packet->GetOutValCount()==0;
  if(!aMeshData->modernMasks)emptyTopology=emptyTopology && aMeshData->_faceAttributeMask8_4.GetOutValCount()==0;

  ///////////////////////////////////////////////////////////////////
  // Topologically Compressed Vertex Records data collection
  ///////////////////////////////////////////////////////////////////

  // Read parameters
  VertexBinding2 aVertexBindings;
  QuantizationParams aQuantParam;
  Jt_I32 aNumberOfVert, aNumberOfAttr;

  if (!aVertexBindings.Read (theReader)
   || !aQuantParam.Read     (theReader)
   || !theReader.ReadI32 (aNumberOfVert))
  {
    return Standard_False;
  }

  if (aNumberOfVert == 0)
  {
    if(!emptyTopology)
      throw std::runtime_error("Zero-count JT coordinates have nonempty topology");
    myExplicitEmpty=true;
    return Standard_True;
  }

  if (!theReader.ReadI32 (aNumberOfAttr))
    return Standard_False;

  if(aNumberOfVert<0 || aNumberOfVert>16000000 || aNumberOfAttr<0 || aNumberOfAttr>16000000)
    throw std::runtime_error("Invalid JT topological vertex/attribute count");

  aDecodeTasks.Run (MeshDecodeTask (aMeshData, aVertexIndices, aNormalIndices, myFaceGroups));

  Standard_Boolean aCoordsPresent = (aVertexBindings.NbVertexCoordComponents() > 0);
  Standard_Boolean aNormalsPresent = aVertexBindings.IsNormalBinding();
  const unsigned aColorComponents=unsigned(aVertexBindings.NbColorComponents());

  // Read vertex coords data and start decoding it
  int32_t aUniqueVerticesCount = 0;
  VertexData aUniqueVertices;
  if (aCoordsPresent)
  {
    JtDecode_VertexData::Handle anEncodedVertices =
      JtDecode_VertexData::LoadCompressedCoords (theReader);

    if (!anEncodedVertices)
      return Standard_False;

    aUniqueVerticesCount = anEncodedVertices->GetOutVertexCount();
    aDecodeTasks.Run (VertexDataDecodeTask (anEncodedVertices, aUniqueVertices));
  }

  // Read vertex normals data and start decoding it
  VertexData aUniqueNormals;
  if (aNormalsPresent)
  {
    JtDecode_VertexData::Handle anEncodedNormals =
      JtDecode_VertexData::LoadCompressedNormals (theReader);

    if (!anEncodedNormals)
      return Standard_False;

    aDecodeTasks.Run (VertexDataDecodeTask (anEncodedNormals, aUniqueNormals));
  }

  VertexData aUniqueColors;
  if(aColorComponents)
    aUniqueColors=JtDecode_VertexColors::Read(theReader,aColorComponents,aNumberOfAttr);

  TextureData aUniqueUV;
  aVertexBindings.TextureChannelCount(); // validate every slot before consuming collections
  for(unsigned channel=0;channel<MaxTextureChannels;++channel)
    if(const auto components=aVertexBindings.NbTextCoordComponents(channel))
      aUniqueUV[channel]=JtDecode_VertexUV::Read(theReader,unsigned(components),aNumberOfAttr);

  aDecodeTasks.Wait();
  if(aCoordsPresent && aUniqueVertices.Count()!=aNumberOfVert)
    throw std::runtime_error("JT coordinate count disagrees with topology");
  if(aNormalsPresent && aUniqueNormals.Count()!=aNumberOfAttr)
    throw std::runtime_error("JT normal count disagrees with attributes");
  assembleVertexRecords(aUniqueVertices,aUniqueNormals,aUniqueColors,aUniqueUV,aVertexIndices,aNormalIndices);
  return Standard_True;
}

// Original ModelViewer adaptation: every topology attribute record may contain
// normal, colour and UV data. Preserve the coordinate/attribute pair, including
// colour seams whose positions and numerical normal values happen to coincide.
void JtElement_ShapeLOD_Vertex::assembleVertexRecords(
    const VertexData& coords,const VertexData& normals,const VertexData& colors,const TextureData& uv,
    const IndicesVec& vertices,const IndicesVec& attributes)
{
  myVertices.Free();myNormals.Free();myColors.Free();for(auto& channel:myTexCoords)channel.Free();myIndices.Free();
  if(coords.Count()<=0 || coords.CompCount()!=3)
    throw std::runtime_error("Unsupported JT coordinate binding");
  const bool hasNormals=normals.Count()!=0,hasColors=colors.Count()!=0;
  int uvCount=0;
  for(const auto& channel:uv)if(channel.Count()){uvCount=channel.Count();break;}
  const bool hasUV=uvCount!=0;
  if((hasNormals && normals.CompCount()!=3) || (hasColors && colors.CompCount()!=3 && colors.CompCount()!=4)
     || (hasNormals && hasColors && normals.Count()!=colors.Count()))
    throw std::runtime_error("Invalid JT attribute record layout");
  const int attributeCount=hasNormals?normals.Count():hasColors?colors.Count():uvCount;
  for(const auto& channel:uv){
    if(!channel.Count())continue;
    if((channel.CompCount()!=1 && channel.CompCount()!=2) || channel.Count()!=attributeCount)
      throw std::runtime_error("Invalid JT texture-coordinate attribute layout");
  }
  if((hasNormals || hasColors || hasUV) && attributes.Count()!=vertices.Count())
    throw std::runtime_error("JT corner attribute index count mismatch");
  if(!hasNormals && !hasColors && !hasUV) {
    for(int i=0;i<vertices.Count();++i)
      if(vertices[i]<0 || vertices[i]>=coords.Count()) throw std::runtime_error("JT coordinate index out of range");
    myVertices=coords;myIndices=vertices;return;
  }
  JtData_Limits::allocation(size_t(vertices.Count())*80); // conservative map-node budget
  std::map<std::pair<int,int>,int> pairs;
  IndicesVec representatives(vertices.Count());myIndices.Allocate(vertices.Count());
  int count=0;
  for(int i=0;i<vertices.Count();++i) {
    JtData_Limits::poll();
    const int v=vertices[i],a=(hasNormals || hasColors || hasUV)?attributes[i]:0;
    if(v<0 || v>=coords.Count() || ((hasNormals || hasColors || hasUV) && (a<0 || a>=attributeCount)))
      throw std::runtime_error("JT corner coordinate/attribute index out of range");
    const auto inserted=pairs.emplace(std::make_pair(v,a),count);
    if(inserted.second) representatives[count++]=i;
    myIndices[i]=inserted.first->second;
  }
  myVertices.Allocate(count,3);
  if(hasNormals) myNormals.Allocate(count,3);
  if(hasColors) myColors.Allocate(count,colors.CompCount());
  for(unsigned channel=0;channel<MaxTextureChannels;++channel)
    if(uv[channel].Count())myTexCoords[channel].Allocate(count,uv[channel].CompCount());
  for(int i=0;i<count;++i) {
    const int corner=representatives[i];
    myVertices[i]=coords[vertices[corner]];
    if(hasNormals) myNormals[i]=normals[attributes[corner]];
    if(hasColors) myColors[i]=colors[attributes[corner]];
    for(unsigned channel=0;channel<MaxTextureChannels;++channel)
      if(uv[channel].Count())myTexCoords[channel][i]=uv[channel][attributes[corner]];
  }
}

//=======================================================================
//function : readTopoMeshCompressedRepData
//purpose  : Polyline / point set LOD geometry (JT 10, TopoMesh Compressed Rep Data, version 1)
//=======================================================================
// Layout, from the NX-written C3 samples (JT 10.10) and the public structure description; unproven for other writers:
//   I32 face group count, I32 primitive count, I32 vertex list length,
//   CDP2 face group boundaries (count + 1 values, indices into the primitive list),
//   CDP2 primitive boundaries (count + 1 values, indices into the vertex list), CDP2 vertex list,
//   I32 hash, vertex binding (8 bytes), 4 quantization bytes, I32 vertex record count,
//   CDP2 records-per-coordinate list, I32 (observed = unique coordinate count), compressed coordinates, [normals, colours].
Standard_Boolean JtElement_ShapeLOD_Vertex::readTopoMeshCompressedRepData (JtData_Reader& reader, PrimitiveKind kind)
{
  const bool lines = (kind == PrimitiveKind::Lines);
  const char* const what = lines ? "polyline" : "point set";
  const auto fail = [&](const std::string& message) { throw std::runtime_error(std::string("JT ") + what + " LOD: " + message); };

  Jt_I32 groupCount = 0, primitiveCount = 0, listCount = 0;
  if (!reader.ReadI32 (groupCount) || !reader.ReadI32 (primitiveCount) || !reader.ReadI32 (listCount))
    return Standard_False;
  if (groupCount < 0 || groupCount > 16000000 || primitiveCount < 0 || primitiveCount > 16000000 || listCount < 0 || listCount > 16000000)
    fail("invalid primitive or vertex list size");

  const auto readList = [&](Jt_I32 expected, const char* name) {
    JtDecode_Int32CDP packet;
    if (!packet.Load2 (reader))
      fail(std::string("cannot read the ") + name);
    const Jt_I32 count = packet.GetOutValCount();
    if (count != expected)
      fail(std::string(name) + " holds " + std::to_string(count) + " values, expected " + std::to_string(expected));
    return packet.DecodeI32 (JtDecode_Unpack_Lag1);   // JT predictors apply from the fifth value on
  };
  Jt_VecI32 groupBounds (readList (groupCount + 1, "face group list"));
  Jt_VecI32 primitiveBounds (readList (primitiveCount + 1, "primitive list"));
  Jt_VecI32 vertexList (readList (listCount, "vertex list"));

  Jt_I32 hash, recordCount, uniqueCount;
VertexBinding2 binding;
  QuantizationParams quantization;
  if (!reader.ReadI32 (hash) || !binding.Read (reader) || !quantization.Read (reader) || !reader.ReadI32 (recordCount))
    return Standard_False;
  if (recordCount < 0 || recordCount > 16000000)
    fail("invalid vertex record count");
  if (recordCount == 0)
  {
    if (listCount != 0)
      fail("zero vertex records with a nonempty vertex list");
    myExplicitEmpty = true;
    return Standard_True;
  }

  JtDecode_Int32CDP lengthPacket;
  Jt_I32 trailer;
  if (!lengthPacket.Load2 (reader))
    fail("cannot read the records-per-coordinate list");
  uniqueCount = lengthPacket.GetOutValCount();
  if (uniqueCount < 1 || uniqueCount > recordCount)
    fail("invalid unique coordinate list");
  Jt_VecI32 lengths (lengthPacket.DecodeI32 (JtDecode_Unpack_Null));
  if (!reader.ReadI32 (trailer))   // observed equal to the unique coordinate count; not validated
    return Standard_False;

  if (binding.NbVertexCoordComponents() == 0)
    fail("no vertex coordinates");
  JtDecode_VertexData::Handle encodedCoords = JtDecode_VertexData::LoadCompressedCoords (reader);
  if (!encodedCoords)
    return Standard_False;
  if (encodedCoords->GetOutVertexCount() != uniqueCount)
    fail("unique coordinate count disagrees with the coordinate array");
  VertexData uniqueCoords (encodedCoords->Decode());
  if (uniqueCoords.Count() != uniqueCount || uniqueCoords.CompCount() != 3)
    fail("unsupported coordinate layout");

  VertexData normals, colours;
  if (binding.IsNormalBinding())
  {
    JtDecode_VertexData::Handle encodedNormals = JtDecode_VertexData::LoadCompressedNormals (reader);
    if (!encodedNormals)
      return Standard_False;
    normals = encodedNormals->Decode();
    if (normals.Count() != recordCount)
      fail("normal count disagrees with the vertex records");
  }
  if (const unsigned colourComponents = unsigned (binding.NbColorComponents()))
    colours = JtDecode_VertexColors::Read (reader, colourComponents, recordCount, false);
  if (binding.TextureChannelCount() != 0 || binding.IsFlagBinding() || binding.IsAuxFieldBinding())
    fail("texture, flag and auxiliary vertex attributes are not supported");

  // Vertex record -> unique coordinate.
  std::vector<int> recordCoordinate;
  recordCoordinate.reserve (size_t (recordCount));
  for (int u = 0; u < uniqueCount; ++u)
  {
    if (lengths[u] < 1 || size_t (recordCoordinate.size()) + size_t (lengths[u]) > size_t (recordCount))
      fail("records-per-coordinate list disagrees with the vertex record count");
    for (int k = 0; k < lengths[u]; ++k)
      recordCoordinate.push_back (u);
  }
  if (int (recordCoordinate.size()) != recordCount)
    fail("records-per-coordinate list disagrees with the vertex record count");

  JtData_Limits::allocation (size_t (recordCount) * 3 * sizeof (float));
  myVertices.Allocate (recordCount, 3);
  for (int i = 0; i < recordCount; ++i)
    myVertices[i] = uniqueCoords[recordCoordinate[i]];
  if (normals.Count())
    myNormals = normals;
  if (colours.Count())
  {
    if (colours.Count() != recordCount)
      fail("colour count disagrees with the vertex records");
    myColors = colours;
  }

  for (int i = 0; i < listCount; ++i)
    if (vertexList[i] < 0 || vertexList[i] >= recordCount)
      fail("vertex list index out of range");

  const auto checkBounds = [&](const Jt_VecI32& bounds, Jt_I32 last, const char* name) {
    if (bounds.Count() < 1 || bounds[0] != 0 || bounds[bounds.Count() - 1] != last)
      fail(std::string(name) + " does not span its target list");
    for (int i = 1; i < bounds.Count(); ++i)
      if (bounds[i] < bounds[i - 1])
        fail(std::string(name) + " is not ascending");
  };
  checkBounds (primitiveBounds, listCount, "primitive list");

  checkBounds (groupBounds, primitiveCount, "face group list");
  if (!lines)
  {
    // Every vertex list entry is one point.
    JtData_Limits::allocation (size_t (listCount) * sizeof (int32_t));
    myIndices.Allocate (listCount);
    for (int i = 0; i < listCount; ++i)
      myIndices[i] = vertexList[i];
    return Standard_True;
  }

  size_t segments = 0;
  for (int p = 0; p < primitiveCount; ++p)
  {
    const int vertices = primitiveBounds[p + 1] - primitiveBounds[p];
    if (vertices < 2)
      fail("a polyline has fewer than two vertices");
    segments += size_t (vertices - 1);
    if (segments > 16000000)
      fail("segment limit exceeded");
  }
  JtData_Limits::allocation (segments * 2 * sizeof (int32_t));
  myIndices.Allocate (int (segments * 2));
  const bool grouped = groupCount > 1;
  if (grouped)
    myFaceGroups.Allocate (int (segments));
  int output = 0, outputSegment = 0, group = 0;
  for (int p = 0; p < primitiveCount; ++p)
  {
    JtData_Limits::poll();
    while (group + 1 < groupCount && p >= groupBounds[group + 1])
      ++group;
    for (int j = primitiveBounds[p]; j + 1 < primitiveBounds[p + 1]; ++j)
    {
      myIndices[output++] = vertexList[j];
      myIndices[output++] = vertexList[j + 1];
      if (grouped)
        myFaceGroups[outputSegment] = group;
      ++outputSegment;
    }
  }
  return Standard_True;
}
