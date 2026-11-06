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
// JT 9.5 qualification and shared attribute/codec bounds updated: 2026-10-20.
// Changes: modern OCCT compatibility, JT 10.0 decoding and bounded input handling;
// see src/ThirdParty/occt-jt/MODIFICATIONS.md and PROVENANCE.json for provenance.

#include <JtAttribute_Base.hxx>
#include <JtData_Reader.hxx>

IMPLEMENT_STANDARD_HANDLE (JtAttribute_Base, JtData_Object)
IMPLEMENT_STANDARD_RTTIEXT(JtAttribute_Base, JtData_Object)

//=======================================================================
//function : JtAttribute_Base
//purpose  :
//=======================================================================
JtAttribute_Base::JtAttribute_Base()
: myPaletteIndex      (-1)
, myVersion           (0)
, myStateFlags        (0)
, myFieldInhibitFlags (0)
, myFieldFinalFlags (0) {}

//=======================================================================
//function : Read
//purpose  : Read this entity from a translate file
//=======================================================================
Standard_Boolean JtAttribute_Base::Read (JtData_Reader& theReader)
{
  if (!JtData_Object::Read (theReader))
    return Standard_False;

  if (theReader.Model()->MajorVersion() > 8
  && !theReader.ReadVersion(myVersion))
    return Standard_False;

  if (theReader.Model()->MajorVersion() == 9 && myVersion != 1) throw std::runtime_error("Unsupported JT 9.x base attribute version");
  if (theReader.Model()->MajorVersion() >= 10 && myVersion != 1 && myVersion != 2) throw std::runtime_error("Unsupported JT base attribute version");

  return theReader.ReadU8  (myStateFlags)
      && theReader.ReadU32 (myFieldInhibitFlags)
      && (theReader.Model()->MajorVersion() < 10 || theReader.ReadU32(myFieldFinalFlags));
}

//=======================================================================
//function : Dump
//purpose  :
//=======================================================================
Standard_Integer JtAttribute_Base::Dump (Standard_OStream& theStream) const
{
  theStream << "JtAttribute_Base ";
  return JtData_Object::Dump (theStream);
}

// The palette index belongs after the complete derived payload (reference 5.4).
Standard_Boolean JtAttribute_Base::ReadFieldsV2 (JtData_Reader& theReader)
{
  myPaletteIndex = -1;
  if (myVersion != 2) return Standard_True;
  if (!theReader.ReadI32(myPaletteIndex)) return Standard_False;
  if (myPaletteIndex < -1) throw std::runtime_error("Invalid JT attribute palette index");
  return Standard_True;
}
