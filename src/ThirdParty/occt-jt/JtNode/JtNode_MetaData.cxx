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

#include <JtNode_MetaData.hxx>
#include <JtData_Reader.hxx>

IMPLEMENT_STANDARD_HANDLE (JtNode_MetaData, JtNode_Group)
IMPLEMENT_STANDARD_RTTIEXT(JtNode_MetaData, JtNode_Group)

IMPLEMENT_OBJECT_CLASS(JtNode_MetaData, "MetaData Object",
                       "ce357245-38fb-11d1-a5-06-00-60-97-bd-c6-e1")

//=======================================================================
//function : Read
//purpose  : Read this entity from a translate file
//=======================================================================
Standard_Boolean JtNode_MetaData::Read (JtData_Reader& theReader)
{
  Jt_I16 aVersion;
  return JtNode_Group::Read (theReader)
      && theReader.ReadVersion(aVersion);
}

//=======================================================================
//function : Dump
//purpose  :
//=======================================================================
Standard_Integer JtNode_MetaData::Dump (Standard_OStream& theStream) const
{
  theStream << "JtNode_MetaData ";
  return JtNode_Group::Dump (theStream);
}
