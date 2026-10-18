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

#include <JtProperty_Base.hxx>
#include <JtData_Reader.hxx>

IMPLEMENT_STANDARD_HANDLE (JtProperty_Base, JtData_Object)
IMPLEMENT_STANDARD_RTTIEXT(JtProperty_Base, JtData_Object)

IMPLEMENT_OBJECT_CLASS(JtProperty_Base, "Base property",
                       "10dd104b-2ac8-11d1-9b-6b-00-80-c7-bb-59-97")

//=======================================================================
//function : JtProperty_Base
//purpose  :
//=======================================================================
JtProperty_Base::JtProperty_Base()
: myVersion    (0),
  myStateFlags (0) {}

//=======================================================================
//function : Read
//purpose  : Read this entity from a translate file
//=======================================================================
Standard_Boolean JtProperty_Base::Read (JtData_Reader& theReader)
{
  if (!JtData_Object::Read (theReader))
    return Standard_False;

  if (theReader.Model()->MajorVersion() > 8
  && !theReader.ReadVersion(myVersion))
    return Standard_False;

  return theReader.ReadU32 (myStateFlags);
}

//=======================================================================
//function : Dump
//purpose  :
//=======================================================================
Standard_Integer JtProperty_Base::Dump (Standard_OStream& theStream) const
{
  return JtData_Object::Dump (theStream);
}
