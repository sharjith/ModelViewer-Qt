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
// Changes: modern OCCT compatibility, JT 10.0 decoding and bounded input handling;
// see third_party/occt-jt/MODIFICATIONS.md and PROVENANCE.json for provenance.

#include <JtElement_ShapeLOD_Vertex.hxx>

#include <NCollection_Handle.hxx>

#include <JtData_Reader.hxx>
#include <JtData_Inflate.hxx>

#include <JtDecode_Int32CDP.hxx>
#include <JtDecode_VertexData.hxx>
#include <JtDecode_VertexData_Quantized.hxx>
#include <JtDecode_VertexColors.hxx>
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

public:
  MeshDecodeTask (JtData_SingleHandle<JtDecode_MeshCoderDriver::InputData> theData,
                  IndicesVec& theVertexIndices,
                  IndicesVec& theNormalIndices)
    : myData             (theData)
    , myVertexIndicesPtr (&theVertexIndices)
    , myNormalIndicesPtr (&theNormalIndices) {}

  void operator ()() const
  {
    JtDecode_MeshCoderDriver aMeshCoderDriver;
    aMeshCoderDriver.SetInputData (const_cast <JtDecode_MeshCoderDriver::InputData&> (*myData));
    aMeshCoderDriver.Decode       (myVertexIndicesPtr, myNormalIndicesPtr);
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
  switch ((myTextFlags >> (theTexture * 4)) & 0x0F)
  {
  case 1 << 0: return 1;
  case 1 << 1: return 2;
  case 1 << 2: return 3;
  case 1 << 3: return 4;
  default: return 0;
  }
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
  Standard_Boolean theIsTriStripSet)
{
  myIndices.Free();
  myVertices.Free();
  myNormals.Free();
  myColors.Free();
  myTexCoords.Free();

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
        || guid != Jt_GUID("f830a5ad-be4c-4fbc-9b-5f-b9-26-92-78-d2-e1")
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
    else
      // TODO: Read TopoMesh Compressed Rep Data
      return Standard_True;
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
    if(uvBinding)myTexCoords=legacyUV(reader,count);
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
  aDecodeTasks.Run (MeshDecodeTask (aMeshData, aVertexIndices, aNormalIndices));

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
    return Standard_True;

  if (!theReader.ReadI32 (aNumberOfAttr))
    return Standard_False;

  if(aNumberOfVert<0 || aNumberOfVert>16000000 || aNumberOfAttr<0 || aNumberOfAttr>16000000)
    throw std::runtime_error("Invalid JT topological vertex/attribute count");

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

  aDecodeTasks.Wait();
  if(aCoordsPresent && aUniqueVertices.Count()!=aNumberOfVert)
    throw std::runtime_error("JT coordinate count disagrees with topology");
  if(aNormalsPresent && aUniqueNormals.Count()!=aNumberOfAttr)
    throw std::runtime_error("JT normal count disagrees with attributes");
  assembleVertexRecords(aUniqueVertices,aUniqueNormals,aUniqueColors,aVertexIndices,aNormalIndices);
  return Standard_True;
}

// Original ModelViewer adaptation: every topology attribute record may contain
// normal, colour and UV data. Preserve the coordinate/attribute pair, including
// colour seams whose positions and numerical normal values happen to coincide.
void JtElement_ShapeLOD_Vertex::assembleVertexRecords(
    const VertexData& coords,const VertexData& normals,const VertexData& colors,
    const IndicesVec& vertices,const IndicesVec& attributes)
{
  myVertices.Free();myNormals.Free();myColors.Free();myIndices.Free();
  if(coords.Count()<=0 || coords.CompCount()!=3)
    throw std::runtime_error("Unsupported JT coordinate binding");
  const bool hasNormals=normals.Count()!=0,hasColors=colors.Count()!=0;
  if((hasNormals && normals.CompCount()!=3) || (hasColors && colors.CompCount()!=3 && colors.CompCount()!=4)
     || (hasNormals && hasColors && normals.Count()!=colors.Count()))
    throw std::runtime_error("Invalid JT attribute record layout");
  const int attributeCount=hasNormals?normals.Count():colors.Count();
  if((hasNormals || hasColors) && attributes.Count()!=vertices.Count())
    throw std::runtime_error("JT corner attribute index count mismatch");
  if(!hasNormals && !hasColors) {
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
    const int v=vertices[i],a=(hasNormals || hasColors)?attributes[i]:0;
    if(v<0 || v>=coords.Count() || ((hasNormals || hasColors) && (a<0 || a>=attributeCount)))
      throw std::runtime_error("JT corner coordinate/attribute index out of range");
    const auto inserted=pairs.emplace(std::make_pair(v,a),count);
    if(inserted.second) representatives[count++]=i;
    myIndices[i]=inserted.first->second;
  }
  myVertices.Allocate(count,3);
  if(hasNormals) myNormals.Allocate(count,3);
  if(hasColors) myColors.Allocate(count,colors.CompCount());
  for(int i=0;i<count;++i) {
    const int corner=representatives[i];
    myVertices[i]=coords[vertices[corner]];
    if(hasNormals) myNormals[i]=normals[attributes[corner]];
    if(hasColors) myColors[i]=colors[attributes[corner]];
  }
}
