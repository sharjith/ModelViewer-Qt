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

#ifndef _JtAttribute_Base_HeaderFile
#define _JtAttribute_Base_HeaderFile

#include <JtData_Object.hxx>

//! The base class for Jt attributes.
class JtAttribute_Base : public JtData_Object
{
public:
  //! Default constructor.
  Standard_EXPORT JtAttribute_Base();

  //! Read this entity from a JT file.
  Standard_EXPORT virtual Standard_Boolean Read (JtData_Reader& theReader);

  //! Dump this entity.
  Standard_EXPORT virtual Standard_Integer Dump (Standard_OStream& theStream) const;

  Jt_I16 BaseVersion() const { return myVersion; }
  Jt_I32 PaletteIndex() const { return myPaletteIndex; }

  bool AccumulationIgnored() const { return (myStateFlags & 0x04) != 0; }
  bool AccumulationForced() const { return (myStateFlags & 0x02) != 0; }
  Jt_U32 FieldInhibitFlags() const { return myFieldInhibitFlags; }
  Jt_U32 FieldFinalFlags() const { return myFieldFinalFlags; }

  DEFINE_STANDARD_RTTIEXT(JtAttribute_Base, JtData_Object)

protected:
  //! Version-2 fields follow the derived attribute payload, not the base header.
  Standard_EXPORT Standard_Boolean ReadFieldsV2 (JtData_Reader& theReader);

  Jt_I32 myPaletteIndex;
  Jt_I16 myVersion;
  Jt_U8  myStateFlags;
  Jt_U32 myFieldInhibitFlags;
  Jt_U32 myFieldFinalFlags;
};

DEFINE_STANDARD_HANDLE(JtAttribute_Base, JtData_Object)

#endif // _JtAttribute_Base_HeaderFile
