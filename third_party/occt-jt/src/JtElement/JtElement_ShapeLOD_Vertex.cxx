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

#include <JtElement_ShapeLOD_Vertex.hxx>

#include <NCollection_Handle.hxx>

#include <JtData_Reader.hxx>
#include <JtData_Inflate.hxx>

#include <JtDecode_Int32CDP.hxx>
#include <JtDecode_VertexData.hxx>
#include <JtDecode_MeshCoderDriver.hxx>

#include <map>
#include <vector>

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
  default: return 0;
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

  if (theReader.Model()->MajorVersion() < 9)
  {
    if (!JtData_Object::Read (theReader))
      return Standard_False;

    if (theReader.Model()->MajorVersion() > 7)
    {
      Jt_I16 aVersion;
      if (!theReader.ReadVersion(aVersion))
        return Standard_False;
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
Standard_Boolean JtElement_ShapeLOD_Vertex::readVertexBasedShapeCompressedRepData (JtData_Reader& theReader)
{
  // The Vertex Based Shape Compressed Rep Data collection is the
  // compressed and/or encoded representation of the vertex coordinates,
  // normal, texture coordinate, and color data for a vertex based shape. p.234

  // - Version Number is the version identifier for this Vertex Based Shape Rep Data.
  //   Version number "0x0001" is currently the only valid value.
  // - Normal Binding specifies how (at what granularity) normal vector data is supplied ("bound") for the Shape Rep
  //   in either the Lossless Compressed Raw Vertex Data
  // - Texture Coord Binding specifies how (at what granularity) texture coordinate data is supplied ("bound")
  //   for the Shape Rep in either the Lossless Compressed Raw Vertex Data or Lossy Quantized Raw Vertex Data collections.
  // - Color Binding specifies how (at what granularity) color data is supplied ("bound") for the Shape Rep in either the Lossless
  //   Compressed Raw Vertex Data or Lossy Quantized Raw Vertex Data collections.
  Jt_I16 aVersionNumber;
  Jt_U8 aNormalBinding, aTextCoordBinding, aColorBinding;
  QuantizationParams aQuantParam;
  if (!theReader.ReadVersion(aVersionNumber)
   || !theReader.ReadU8  (aNormalBinding)
   || !theReader.ReadU8  (aTextCoordBinding)
   || !theReader.ReadU8  (aColorBinding)
   || !aQuantParam.Read  (theReader))
  {
    return Standard_False;
  }

  /* Primitive List Indices is a vector of indices into the uncompressed
   * Raw Vertex Data marking the start/beginning of primitives.
   * The Primitive List Indices array uses the Int32 version of the CODEC
   * to compress and encode data. */
  Jt_VecI32 aPimitiveListIndices;
  {
    JtDecode_Int32CDP anEncodedPimitiveListIndices;
    if (!anEncodedPimitiveListIndices.Load1 (theReader))
        return Standard_False;

    aPimitiveListIndices = anEncodedPimitiveListIndices.DecodeI32 (JtDecode_Unpack_Stride1);
  }
  Jt_I32 aNbPrimitives = aPimitiveListIndices.Count() - 1;
  Jt_I32 aNbFaces = aPimitiveListIndices.Last() - aPimitiveListIndices.First() - aNbPrimitives * 2;

  // Read raw vertex data
  Jt_VecI32 aVertexDataIndices;
  if (aQuantParam.BitsPerVertex() == 0)
  {
    // lossless compressed raw vertex data

    // read sizes
    Jt_I32 anUncompressedSize, aCompressedSize;
    if (!theReader.ReadI32 (anUncompressedSize)
     || !theReader.ReadI32 (aCompressedSize))
    {
      return Standard_False;
    }

    // select appropriate reader for reading the segment data
    JtData_Reader* aDataReaderPtr;
    if (aCompressedSize > 0)
      // use JtData_Inflate is the data is compressed
      aDataReaderPtr = new JtData_Inflate (theReader, aCompressedSize);
    else
      // use the original reader otherwise
      aDataReaderPtr = &theReader;

    // determine number of vertices
    VertexData::SizeType aVertexCount = aPimitiveListIndices.Last();

    // allocate output storage
    JtData_CompVector <Jt_F32, VertexData::SizeType, VertexData::CompCountType>
      aVertices (aVertexCount                     , 3),
      aNormals  (aNormalBinding ? aVertexCount : 0, 3);

    // read the data
    for (VertexData::SizeType i = 0; i < aVertexCount; i++)
    {
      if (aNormalBinding == VertexBinding1::PerVertex)
        aDataReaderPtr->ReadArray (aNormals[i]);

      aDataReaderPtr->ReadArray   (aVertices[i]);
    }

    // move the data to the output vectors
    myVertices << reinterpret_cast<VertexData&> (aVertices);
    myNormals  << reinterpret_cast<VertexData&> (aNormals);

    // free the JtData_Inflate reader if it was used
    if (aDataReaderPtr != &theReader)
      delete aDataReaderPtr;

    // generate conventional OpenGL arrays
    myIndices.Allocate (aNbFaces * 3);

    Jt_I32 aPrimIdx, anOrigin;
    Jt_I64 anOffset1 = 1;
    Jt_I64 anOffset2 = 2;
    IndicesVec::SizeType aVertIdx = 0;
    for (aPrimIdx = 0; aPrimIdx < aNbPrimitives; aPrimIdx++)
    {
      for (anOrigin = aPimitiveListIndices[aPrimIdx]; anOrigin < aPimitiveListIndices[aPrimIdx + 1] - 2; ++anOrigin)
      {
        myIndices[aVertIdx++] = anOrigin;
        myIndices[aVertIdx++] = anOrigin + anOffset1;
        myIndices[aVertIdx++] = anOrigin + anOffset2;
        anOffset1 ^= 1 ^ 2;
        anOffset2 ^= 1 ^ 2;
      }
    }
  }
  else
  {
    /*
     *Read the lossy quantized raw vertex data
     */

    // create a TBB task group for parallel decoding of read data
    JtData_Parallel::TaskGroup aDecodeTasks;

    // read unique vertex coords data and start decoding it
    {
      JtDecode_VertexData::Handle anEncodedVertices =
        JtDecode_VertexData::LoadQuantizedCoords (theReader);

      if (!anEncodedVertices)
        return Standard_False;

      aDecodeTasks.Run (VertexDataDecodeTask (anEncodedVertices, myVertices));
    }

    // read unique vertex normals data if present and start decoding it
    if (aNormalBinding != VertexBinding1::None)
    {
      JtDecode_VertexData::Handle anEncodedNormals =
        JtDecode_VertexData::LoadQuantizedNormals (theReader);

      if (!anEncodedNormals)
        return Standard_False;

      aDecodeTasks.Run (VertexDataDecodeTask (anEncodedNormals, myNormals));
    }

    /*
     * Read vertex data indices.
     * The Compressed Vertex Index List uses the Int32 version of the coded to compress
     * and encode data.
     */
    {
      JtDecode_Int32CDP anEncodedVertexDataIndices;
      if (!anEncodedVertexDataIndices.Load1 (theReader))
          return Standard_False;

      aVertexDataIndices = anEncodedVertexDataIndices.DecodeI32 (JtDecode_Unpack_StripIdx);
    }

    // generate conventional OpenGL arrays
    myIndices.Allocate (aNbFaces * 3);

    Jt_I32 aPrimIdx, anOrigin;
    Jt_I64 anOffset1 = 1;
    Jt_I64 anOffset2 = 2;
    IndicesVec::SizeType aVertIdx = 0;
    for (aPrimIdx = 0; aPrimIdx < aNbPrimitives; aPrimIdx++)
    {
      for (anOrigin = aPimitiveListIndices[aPrimIdx]; anOrigin < aPimitiveListIndices[aPrimIdx + 1] - 2; ++anOrigin)
      {
        myIndices[aVertIdx++] = aVertexDataIndices[anOrigin];
        myIndices[aVertIdx++] = aVertexDataIndices[anOrigin + anOffset1];
        myIndices[aVertIdx++] = aVertexDataIndices[anOrigin + anOffset2];
        anOffset1 ^= 1 ^ 2;
        anOffset2 ^= 1 ^ 2;
      }
    }

    // wait until all vectors are decoded
    aDecodeTasks.Wait();
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

  Standard_Boolean aCoordsPresent = (aVertexBindings.NbVertexCoordComponents() > 0);
  Standard_Boolean aNormalsPresent = aVertexBindings.IsNormalBinding();

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

  // Create a temporary vector of maps used to map combination of coord index and normal index
  // to a single common index if both coords and normals are present
  typedef std::map<int32_t, int32_t> Map;
  JtData_Vector<Map, int32_t> aMaps;
  if (aCoordsPresent && aNormalsPresent)
    aMaps.Allocate (aUniqueVerticesCount);

  // Wait until all vectors are decoded
  aDecodeTasks.Wait();

  // If both coords and normals are present,
  // convert the data from separately indexed vectors of unique coords and normals
  // to vectors representing unique combinations of coords and normals with each
  // combination indexed by a single index
  if (aCoordsPresent && aNormalsPresent)
  {
    // build the output single vector of indices
    // and a auxiliary vector of indices of unique combinations of source
    // indices of coords and normals
    myIndices.Allocate (aVertexIndices.Count());
    IndicesVec anIdxIndices (aVertexIndices.Count());
    int32_t aUniquePairsCount = 0;

    for (int32_t anIdxIdx = 0; anIdxIdx < aVertexIndices.Count(); anIdxIdx++)
    {
      int32_t aVertexIdx = aVertexIndices[anIdxIdx];
      int32_t aNormalIdx = aNormalIndices[anIdxIdx];

      Map& aMap = aMaps[aVertexIdx];
      Map::iterator anIt = aMap.find (aNormalIdx);

      if (anIt == aMap.end())
      {
        aMap.insert (anIt, Map::value_type (aNormalIdx, aUniquePairsCount));
        anIdxIndices[aUniquePairsCount] = anIdxIdx;
        myIndices[anIdxIdx] = aUniquePairsCount++;
      }
      else
      {
        myIndices[anIdxIdx] = anIt->second;
      }
    }

    // free the memory used by the vector of maps since it will not be needed anymore
    aMaps.Free();

    // build the output vectors of coords and normals using the auxiliary vector
    myVertices.Allocate (aUniquePairsCount, aUniqueVertices.CompCount());
    myNormals .Allocate (aUniquePairsCount, aUniqueNormals.CompCount());

    for (int32_t aUniquePairIdx = 0; aUniquePairIdx < aUniquePairsCount; aUniquePairIdx++)
    {
      int32_t anIdxIdx = anIdxIndices[aUniquePairIdx];
      myVertices[aUniquePairIdx] = aUniqueVertices[aVertexIndices[anIdxIdx]];
      myNormals [aUniquePairIdx] = aUniqueNormals [aNormalIndices[anIdxIdx]];
    }
  }

  // If only coords are present, use the coords vector and its indices
  else if (aCoordsPresent)
  {
    myVertices << aUniqueVertices;
    myIndices  << aVertexIndices;
  }

  // If only normals are present, use the normals vector and its indices
  else if (aNormalsPresent)
  {
    myNormals << aUniqueNormals;
    myIndices << aNormalIndices;
  }

  // Success
  return Standard_True;
}
