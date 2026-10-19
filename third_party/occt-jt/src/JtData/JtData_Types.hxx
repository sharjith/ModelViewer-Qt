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

// Modified for ModelViewer-Qt, 2026-10-19 (including changes made 2026-10-18).
// Changes: modern OCCT compatibility, JT 10.0 decoding and bounded input handling;
// see third_party/occt-jt/MODIFICATIONS.md and PROVENANCE.json for provenance.

#ifndef _JtData_Types_HeaderFile
#define _JtData_Types_HeaderFile

#include <Standard_Type.hxx>
#include <Standard_Macro.hxx>
#include <JtData_Vector.hxx>

// Basic JT data types

typedef unsigned char Jt_UChar;

typedef uint8_t  Jt_U8;
typedef uint16_t Jt_U16;
typedef uint32_t Jt_U32;
typedef uint64_t Jt_U64;

typedef int16_t  Jt_I16;
typedef int32_t  Jt_I32;
typedef int64_t  Jt_I64;

typedef float    Jt_F32;
typedef double   Jt_F64;

// Type information

struct Jt_Unsigned
{
  static const bool IsSigned = false;
  typedef Jt_U8  T8;
  typedef Jt_U16 T16;
  typedef Jt_U32 T32;
  typedef Jt_U64 T64;
};

struct Jt_Signed
{
  static const bool IsSigned = true;
  typedef Jt_I16 T16;
  typedef Jt_I32 T32;
  typedef Jt_I64 T64;
};

template <class Type> struct Jt_TypeInfo;
template<> struct Jt_TypeInfo<Jt_U8 > {typedef Jt_Unsigned SignClass;};
template<> struct Jt_TypeInfo<Jt_U16> {typedef Jt_Unsigned SignClass;};
template<> struct Jt_TypeInfo<Jt_U32> {typedef Jt_Unsigned SignClass;};
template<> struct Jt_TypeInfo<Jt_U64> {typedef Jt_Unsigned SignClass;};
template<> struct Jt_TypeInfo<Jt_I16> {typedef Jt_Signed SignClass;};
template<> struct Jt_TypeInfo<Jt_I32> {typedef Jt_Signed SignClass;};
template<> struct Jt_TypeInfo<Jt_I64> {typedef Jt_Signed SignClass;};

// Composite JT Data Types

typedef JtData_Vector<Jt_U8 , Jt_I32> Jt_String;
typedef JtData_Vector<Jt_U16, Jt_I32> Jt_MbString;
typedef JtData_Vector<Jt_I32, Jt_I32> Jt_VecI32;
typedef JtData_Vector<Jt_U32, Jt_I32> Jt_VecU32;
typedef JtData_Vector<Jt_F32, Jt_I32> Jt_VecF32;
typedef JtData_Vector<Jt_F64, Jt_I32> Jt_VecF64;

template <class Type>
struct Jt_XYZ
{
  Type X, Y, Z;

  operator JtData_VectorRef<Type, Jt_I32>()
    { return JtData_VectorRef<Type, Jt_I32> (*this); }

  operator JtData_VectorRef<const Type, Jt_I32>() const
    { return JtData_VectorRef<const Type, Jt_I32> (*this); }

  DEFINE_STANDARD_ALLOC
};

typedef Jt_XYZ<Jt_F32> Jt_CoordF32;
typedef Jt_XYZ<Jt_F64> Jt_CoordF64;
typedef Jt_XYZ<Jt_F32> Jt_DirF32;

struct Jt_BBoxF32
{
  Jt_CoordF32 MinCorner, MaxCorner;

  operator JtData_VectorRef<Jt_F32, Jt_I32>()
    { return JtData_VectorRef<Jt_F32, Jt_I32> (*this); }

  operator JtData_VectorRef<const Jt_F32, Jt_I32>() const
    { return JtData_VectorRef<const Jt_F32, Jt_I32> (*this); }

  DEFINE_STANDARD_ALLOC
};

class Jt_GUID
{
public:
  static const int GUIDStringLength = 42;

  union {
    struct{
      Jt_U32 U32;
      Jt_U16 U16[2];
      Jt_U8  U8 [8];
    } codes;
    Jt_U64 U64[2];
  } data;

public:
  Jt_GUID() { memset (this, 0, sizeof (Jt_GUID)); }

  Standard_EXPORT Jt_GUID (Jt_U32 theU32, Jt_U16 theU16_1, Jt_U16 theU16_2,
                           Jt_U8 theU8_1, Jt_U8 theU8_2, Jt_U8 theU8_3, Jt_U8 theU8_4,
                           Jt_U8 theU8_5, Jt_U8 theU8_6, Jt_U8 theU8_7, Jt_U8 theU8_8);

  Standard_EXPORT Jt_GUID (const char* theString);

  Standard_EXPORT Standard_Boolean ToString (char* theString) const;

  Standard_Boolean operator == (const Jt_GUID& theOther) const
    { return ((U64 (0) ^ theOther.U64 (0)) | (U64 (1) ^ theOther.U64 (1))) == 0; }

  Standard_Boolean operator != (const Jt_GUID& theOther) const
    { return !operator == (theOther); }

  size_t operator()(const Jt_GUID& key) const noexcept { return size_t(key.data.U64[0] ^ key.data.U64[1]); }
  bool operator()(const Jt_GUID& a, const Jt_GUID& b) const noexcept { return a == b; }

  Jt_I32 Hash (const Jt_I32 theUpper) const
    { return ((&data.codes.U32)[0] ^ (&data.codes.U32)[1] ^ (&data.codes.U32)[2] ^ (&data.codes.U32)[3]) % theUpper; }

  static Standard_Integer HashCode (const Jt_GUID& theKey, const Standard_Integer theUpper)
    { return theKey.Hash (theUpper); }

  static Standard_Boolean IsEqual (const Jt_GUID& theKey1, const Jt_GUID& theKey2)
    { return theKey1 == theKey2; }

protected:
  Jt_U64 U64 (int theIdx) const { return data.U64[theIdx]; }

public:
  DEFINE_STANDARD_ALLOC
};

#endif
