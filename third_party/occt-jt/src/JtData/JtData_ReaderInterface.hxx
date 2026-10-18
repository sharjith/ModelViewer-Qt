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

#ifndef _JtData_ReaderInterface_HeaderFile
#define _JtData_ReaderInterface_HeaderFile

#include <JtData_Vector.hxx>
#include <JtData_Types.hxx>
#include <JtData_Model.hxx>

#include <TCollection_AsciiString.hxx>
#include <TCollection_ExtendedString.hxx>

//! Static virtual class JtData_ReaderInterface:
//! provides data reading methods for the derived reader class.
class JtData_Reader;

template <class Derived>
class JtData_ReaderInterface
{
public:
  //! Get the associated model.
  const Handle(JtData_Model)& Model() const
    { return reader().Model(); }

  //! Read raw bytes from the stream.
  Standard_Boolean Read (void* theBuffer, Standard_Size theLength)
    { return reader().ReadBytes (theBuffer, theLength); }

  //! Skip some bytes.
  Standard_Boolean Skip (Standard_Size theLength)
    { return reader().SkipBytes (theLength); }

  //! Get absolute reading position.
  Standard_Size GetPosition() const
    { return reader().GetPosition(); }

  //! Read a primitive value from the stream.
  template <class Type>
  Standard_Boolean ReadValue (Type& theValue)
    { return reader().ReadPrimitiveValue (theValue); }

  //! Read given number of signed bytes.
  template <class SizeT>
  Standard_Boolean ReadArray (int8_t* theArray, const SizeT theLength)
    { return reader().ReadPrimitiveArray (theArray, theLength); }

  //! Read given number of unsigned bytes.
  template <class SizeT>
  Standard_Boolean ReadArray (uint8_t* theArray, const SizeT theLength)
    { return reader().ReadPrimitiveArray (theArray, theLength); }

  //! Read given number of signed 16-bit integer values.
  template <class SizeT>
  Standard_Boolean ReadArray (int16_t* theArray, const SizeT theLength)
    { return reader().ReadPrimitiveArray (theArray, theLength); }

  //! Read given number of unsigned 16-bit integer values.
  template <class SizeT>
  Standard_Boolean ReadArray (uint16_t* theArray, const SizeT theLength)
    { return reader().ReadPrimitiveArray (theArray, theLength); }

  //! Read given number of signed 32-bit integer values.
  template <class SizeT>
  Standard_Boolean ReadArray (int32_t* theArray, const SizeT theLength)
    { return reader().ReadPrimitiveArray (theArray, theLength); }

  //! Read given number of unsigned 32-bit integer values.
  template <class SizeT>
  Standard_Boolean ReadArray (uint32_t* theArray, const SizeT theLength)
    { return reader().ReadPrimitiveArray (theArray, theLength); }

  //! Read given number of signed 64-bit integer values.
  template <class SizeT>
  Standard_Boolean ReadArray (int64_t* theArray, const SizeT theLength)
    { return reader().ReadPrimitiveArray (theArray, theLength); }

  //! Read given number of unsigned 64-bit integer values.
  template <class SizeT>
  Standard_Boolean ReadArray (uint64_t* theArray, const SizeT theLength)
    { return reader().ReadPrimitiveArray (theArray, theLength); }

  //! Read given number of float values.
  template <class SizeT>
  Standard_Boolean ReadArray (float* theArray, const SizeT theLength)
    { return reader().ReadPrimitiveArray (theArray, theLength); }

  //! Read given number of double values.
  template <class SizeT>
  Standard_Boolean ReadArray (double* theArray, const SizeT theLength)
    { return reader().ReadPrimitiveArray (theArray, theLength); }

  //! Read given number of objects all having the same type
  //! using a user-defined ReadObject method to read every object.
  template <class Object, class SizeT>
  Standard_Boolean ReadArray (Object* theArray, const SizeT theLength)
  {
    for (SizeT i = 0; i < theLength; i++)
      if (!Read (theArray[i]))
        return Standard_False;

    return Standard_True;
  }

  //! Load/map raw bytes from the stream to memory
  //! and return a pointer to access the data.
  const void* Load (const Standard_Size theLength)
    { return reader().LoadBytes (theLength); }

  //! Free/unmap memory allocated/mapped by the Load method.
  void Unload (const void* theAddress)
    { return reader().UnloadBytes (theAddress); }

  //! Read a 1-byte character string.
  Standard_Boolean ReadString (TCollection_AsciiString& theString)
    { return reader().ReadSbString (theString); }

  //! Read a 2-byte character string.
  Standard_Boolean ReadMbString (TCollection_ExtendedString& theString)
    { return reader().ReadMbString (theString); }

  //! Read a primitive value and convert it to another type.
  template <class SrcType, class DstType>
  Standard_Boolean ReadValue (SrcType theSrc, DstType& theDst)
  {
    Standard_Boolean aResult = ReadValue (theSrc);
    theDst = theSrc;
    return aResult;
  }

  //! Read a referenced array.
  template <class Type, class SizeT>
  Standard_Boolean ReadArray (JtData_VectorRef<Type, SizeT> theArray)
    { return ReadArray (theArray.Data(), theArray.Count()); }

  //! Read a fixed-size array.
  template <class Type, Standard_Size N>
  Standard_Boolean ReadArray (Type (&theArray) [N])
    { return ReadArray (theArray, N); }

  //! Read a structure consisting of elements of the same type.
  template <class ElemType, class Struct>
  Standard_Boolean ReadUniformStruct (Struct& theStruct, const ElemType& = ElemType())
    { return ReadArray (JtData_VectorRef<ElemType> (theStruct)); }

  //! Allocate a vector and read given number of values to it.
  template <class Type, class VecSizeT, class SizeT>
  Standard_Boolean ReadVec (JtData_Vector<Type, VecSizeT>& theVector, const SizeT theLength)
  {
    theVector.Allocate (theLength);
    return ReadArray (theVector.Data(), theLength);
  }

  //! Read a vector.
  template <class Type, class SizeT>
  Standard_Boolean ReadVec (JtData_Vector<Type, SizeT>& theVector)
  {
    SizeT aLength;
    if (!ReadValue (aLength))
      return Standard_False;

    if (!aLength)
    {
      theVector.Free();
      return Standard_True;
    }

    return ReadVec (theVector, aLength);
  }

  //! Read a GUID.
  Standard_EXPORT Standard_Boolean ReadGUID (Jt_GUID& theGUID)
    { return ReadValue (theGUID.data.codes.U32) && ReadArray (theGUID.data.codes.U16) && ReadArray (theGUID.data.codes.U8); }

  // Explicit reading of basic JT data types
  Standard_Boolean ReadUChar (Jt_UChar& theValue) { return ReadValue (theValue); }
  Standard_Boolean ReadU8    (Jt_U8   & theValue) { return ReadValue (theValue); }
  Standard_Boolean ReadU16   (Jt_U16  & theValue) { return ReadValue (theValue); }
  Standard_Boolean ReadU32   (Jt_U32  & theValue) { return ReadValue (theValue); }
  Standard_Boolean ReadU64   (Jt_U64  & theValue) { return ReadValue (theValue); }
  Standard_Boolean ReadI16   (Jt_I16  & theValue) { return ReadValue (theValue); }
  Standard_Boolean ReadI32   (Jt_I32  & theValue) { return ReadValue (theValue); }
  template<class T> Standard_Boolean ReadVersion(T& value) { return Model()->MajorVersion()>=10 ? ReadValue(Jt_U8{}, value) : ReadValue(Jt_I16{}, value); }

  Standard_Boolean ReadI64   (Jt_I64  & theValue) { return ReadValue (theValue); }
  Standard_Boolean ReadF32   (Jt_F32  & theValue) { return ReadValue (theValue); }
  Standard_Boolean ReadF64   (Jt_F64  & theValue) { return ReadValue (theValue); }

  // Reading with conversion to a more precise data type
  Standard_Boolean ReadU8  (Standard_Integer& theValue) { return ReadValue (Jt_U8 (), theValue); }
  Standard_Boolean ReadU8  (Standard_Size   & theValue) { return ReadValue (Jt_U8 (), theValue); }
  Standard_Boolean ReadU16 (Standard_Integer& theValue) { return ReadValue (Jt_U16(), theValue); }
  Standard_Boolean ReadU16 (Standard_Size   & theValue) { return ReadValue (Jt_U16(), theValue); }
  Standard_Boolean ReadI16 (Standard_Integer& theValue) { return ReadValue (Jt_I16(), theValue); }
  Standard_Boolean ReadF32 (Standard_Real   & theValue) { return ReadValue (Jt_F32(), theValue); }

  // Abstract reading of basic JT data types
  Standard_Boolean Read (Jt_U8 & theValue) { return ReadValue (theValue); }
  Standard_Boolean Read (Jt_U16& theValue) { return ReadValue (theValue); }
  Standard_Boolean Read (Jt_U32& theValue) { return ReadValue (theValue); }
  Standard_Boolean Read (Jt_U64& theValue) { return ReadValue (theValue); }
  Standard_Boolean Read (Jt_I16& theValue) { return ReadValue (theValue); }
  Standard_Boolean Read (Jt_I32& theValue) { return ReadValue (theValue); }
  Standard_Boolean Read (Jt_I64& theValue) { return ReadValue (theValue); }
  Standard_Boolean Read (Jt_F32& theValue) { return ReadValue (theValue); }
  Standard_Boolean Read (Jt_F64& theValue) { return ReadValue (theValue); }

  //! Read a referenced array.
  template <class Type, class SizeT>
  Standard_Boolean Read (JtData_VectorRef<Type, SizeT> theArray)
    { return ReadArray (theArray); }

  //! Read a fixed-size array.
  template <class Type, Standard_Size N>
  Standard_Boolean Read (Type (&theArray) [N])
    { return ReadArray (theArray); }

  //! Read a vector.
  template <class Type, class SizeT>
  Standard_Boolean Read (JtData_Vector<Type, SizeT>& theVector)
    { return ReadVec (theVector); }

  //! Read an XYZ type.
  template <class Type>
  Standard_Boolean Read (Jt_XYZ<Type>& theXYZ)
    { return ReadUniformStruct<Type> (theXYZ); }

  //! Read a GUID.
  Standard_Boolean Read (Jt_GUID& theGUID)
    { return ReadGUID (theGUID); }

  //! Read a 1-byte character string.
  Standard_Boolean Read (TCollection_AsciiString& theString)
    { return ReadString (theString); }

  //! Read a 2-byte character string.
  Standard_Boolean Read (TCollection_ExtendedString& theString)
    { return ReadMbString (theString); }

  //! Read an object using a user-defined ReadObject method.
  template <class Object>
  Standard_Boolean Read (Object& theObject)
    { return static_cast<Derived*> (this)->ReadObject (theObject); }

private:
  JtData_Reader& reader() { return *static_cast<Derived*> (this); }
  const JtData_Reader& reader() const { return *static_cast<const Derived*> (this); }
};

#endif // _JtData_ReaderInterface_HeaderFile
