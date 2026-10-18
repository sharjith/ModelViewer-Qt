// JT format reading and visualization tools
// Copyright (C) 2014-2015 OPEN CASCADE SAS
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

#ifndef _JtDecode_VertexData_HeaderFile
#define _JtDecode_VertexData_HeaderFile

#include <JtData_SingleHandle.hxx>
#include <JtData_Vector.hxx>
#include <JtData_CompVector.hxx>
#include <JtData_Reader.hxx>
#include <JtDecode_Int32CDP.hxx>

//! Base class for vertex decoding algorithms.
class JtDecode_VertexData
{
public:

  typedef JtData_SingleHandle<JtDecode_VertexData> Handle;

  typedef JtData_CompVector<float, int32_t> Decoded;

  //! Load lossy quantized vertex coords data.
  Standard_EXPORT static Handle LoadQuantizedCoords   (JtData_Reader& theReader);

  //! Load lossless compressed vertex coords data.
  Standard_EXPORT static Handle LoadCompressedCoords  (JtData_Reader& theReader);

  //! Load lossy quantized vertex normals data.
  Standard_EXPORT static Handle LoadQuantizedNormals  (JtData_Reader& theReader);

  //! Load lossless compressed vertex normals data.
  Standard_EXPORT static Handle LoadCompressedNormals (JtData_Reader& theReader);

  //! Get expected count of output vertices.
  int32_t GetOutVertexCount() { return myPackages.IsEmpty() ? 0 : myPackages[0].GetOutValCount(); }

  //! Get expected count of components in the output vertices.
  Standard_Integer GetOutCompCount() { return getOutCompCount (myPackages.Count()); }

  //! Decode the loaded data. Can be called only once for an instance.
  Decoded::Mover Decode()
  {
    Decoded aResults (GetOutVertexCount(), GetOutCompCount());
    decode (aResults);
    myPackages.Free();
    return aResults.Move();
  }

  //! D-tor
  virtual ~JtDecode_VertexData() {};

protected:

  //! Construct empty data.
  JtDecode_VertexData() {}

  //! Initialize data consisting of the given number of packages.
  JtDecode_VertexData (const Standard_Size theNbPackages, JtDecode_Unpack& theUnpacker)
    : myPackages (theNbPackages), myUnpacker (&theUnpacker) {}

  //! Decode a package with the given index.
  Jt_VecI32::Mover decodePackage (const Standard_Size thePackageNum)
  {
    return myPackages[thePackageNum].DecodeU32 (*myUnpacker);
  }

  //! Create a decoding functor for package with the given index.
  JtDecode_Int32CDP::DecodingFunctor getDecodingFunctor (const Standard_Size thePackageNum,
                                                               Jt_VecU32&    theResult)
  {
    return myPackages[thePackageNum].GetDecodingFunctor (theResult, *myUnpacker);
  }

  //! Get expected count of output components; to be reimplemented in derived classes.
  Standard_EXPORT virtual Standard_Integer getOutCompCount (Standard_Size thePackageCount) = 0;

  //! Decode the whole data; to be reimplemented in derived classes.
  Standard_EXPORT virtual void decode (Decoded::Ref theResults) = 0;

private:

  //! Load the data packages.
  Standard_Boolean load (JtData_Reader&               theReader,
                         JtDecode_Int32CDP::LoadFnPtr theLoader,
                         const Jt_I32                 theVertexCount);

private:

  JtData_Vector<JtDecode_Int32CDP> myPackages;
  JtDecode_Unpack*                 myUnpacker;
};

#endif
