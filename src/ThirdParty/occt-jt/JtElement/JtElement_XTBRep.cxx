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
// see src/ThirdParty/occt-jt/MODIFICATIONS.md and PROVENANCE.json for provenance.

#include <JtElement_XTBRep.hxx>
#include <JtData_Reader.hxx>

#include <TColStd_HArray1OfCharacter.hxx>

IMPLEMENT_STANDARD_HANDLE (JtElement_XTBRep, JtData_Object)
IMPLEMENT_STANDARD_RTTIEXT(JtElement_XTBRep, JtData_Object)

IMPLEMENT_OBJECT_CLASS(JtElement_XTBRep, "XT B-Rep Object",
                       "873a70e0-2ac9-11d1-9b-6b-00-80-c7-bb-59-97")

//=======================================================================
//function : Read
//purpose  : Read this entity from a translate file
//=======================================================================
Standard_Boolean JtElement_XTBRep::Read (JtData_Reader& theReader)
{
  if (theReader.Model()->MajorVersion() >= 10)
  {
    Jt_U8 version, subordinate;
    if (!theReader.ReadU8(version) || !theReader.ReadU8(subordinate)) return Standard_False;
    myVersion = version;
    if (subordinate) throw std::runtime_error("Subordinate JT XT payload requires body-reference resolution");
    if (!theReader.ReadI32(myPKMajorVersion) || !theReader.ReadI32(myPKMinorVersion)
        || !theReader.ReadI32(myPKBuild)) return Standard_False;
    Jt_U8 transmitFormat;
    if (!theReader.ReadU8(transmitFormat)) return Standard_False;
    if (transmitFormat != 0) throw std::runtime_error("Unsupported JT XT transmit format");
    return theReader.ReadVec(myData);
  }
  if (!JtData_Object::Read (theReader)
   || !theReader.ReadI32 (myVersion)
   || !theReader.ReadI32 (myPKMajorVersion)
   || !theReader.ReadI32 (myPKMinorVersion))
  {
    return Standard_False;
  }

  if (theReader.Model()->MajorVersion() > 8
  && !theReader.ReadI32 (myPKBuild))
  {
    return Standard_False;
  }

  return theReader.ReadVec (myData);
}

//=======================================================================
//function : Dump
//purpose  :
//=======================================================================
Standard_Integer JtElement_XTBRep::Dump (Standard_OStream& theStream) const
{
  theStream << "JtElement_XTBRep { " << myPKMajorVersion << "." << myPKMinorVersion << "." << myPKBuild << " } ";
  return JtData_Object::Dump (theStream);
}
