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

#include <JtNode_Base.hxx>
#include <JtData_Reader.hxx>
#include <JtData_DeferredObject.hxx>

IMPLEMENT_STANDARD_HANDLE (JtNode_Base, JtData_Object)
IMPLEMENT_STANDARD_RTTIEXT(JtNode_Base, JtData_Object)

IMPLEMENT_OBJECT_CLASS(JtNode_Base, "Base node",
                       "10dd1035-2ac8-11d1-9b-6b-00-80-c7-bb-59-97")

//=======================================================================
//function : Read
//purpose  : Read this entity from a translate file
//=======================================================================
Standard_Boolean JtNode_Base::Read (JtData_Reader& theReader)
{
  if (!JtData_Object::Read (theReader))
    return Standard_False;

  Jt_I16 aVersion;
  if (theReader.Model()->MajorVersion() > 8
  && !theReader.ReadVersion(aVersion))
    return Standard_False;

  Jt_U32 aFlags;
  if (!theReader.ReadU32 (aFlags))
    return Standard_False;
  myIgnored=(aFlags&1)!=0;

  return JtData_DeferredObject::ReadVec (theReader, myAttributes);
}

//=======================================================================
//function : Dump
//purpose  :
//=======================================================================
Standard_Integer JtNode_Base::Dump (Standard_OStream& theStream) const
{
  theStream << "JtNode_Base ";
  if (!myAttributes.IsEmpty())
    theStream << "{ A:" << myAttributes.Count() << " } ";
  return JtData_Object::Dump (theStream);
}

//=======================================================================
//function : BindObjects
//purpose  : Bind other objects to the object
//=======================================================================
void JtNode_Base::BindObjects (const MapOfObjects& theObjectsMap)
{
  JtData_DeferredObject::ResolveVec (myAttributes, theObjectsMap);
}

//=======================================================================
//function : BindName
//purpose  : Bind a name to the object
//=======================================================================
void JtNode_Base::BindName (const TCollection_ExtendedString& theName)
{
  myName = theName.Token ((Standard_ExtString)L";");
}
