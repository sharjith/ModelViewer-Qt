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

#include <JtNode_Shape_TriStripSet.hxx>
#include <JtData_Reader.hxx>

IMPLEMENT_STANDARD_HANDLE (JtNode_Shape_TriStripSet, JtNode_Shape_Vertex)
IMPLEMENT_STANDARD_RTTIEXT(JtNode_Shape_TriStripSet, JtNode_Shape_Vertex)

IMPLEMENT_OBJECT_CLASS(JtNode_Shape_TriStripSet, "Tri-Strip Set Shape Object",
                       "10dd1077-2ac8-11d1-9b-6b-00-80-c7-bb-59-97")

//=======================================================================
//function : Read
//purpose  : Read this entity from a translate file
//=======================================================================
Standard_Boolean JtNode_Shape_TriStripSet::Read (JtData_Reader& theReader)
{
  return JtNode_Shape_Vertex::Read (theReader);
}

//=======================================================================
//function : Dump
//purpose  :
//=======================================================================
Standard_Integer JtNode_Shape_TriStripSet::Dump (Standard_OStream& theStream) const
{
  theStream << "JtNode_Shape_TriStripSet ";
  return JtNode_Shape_Vertex::Dump (theStream);
}

//=======================================================================
//function : BindLateLoads
//purpose  : Bind a late loaded property to the object
//=======================================================================
void JtNode_Shape_TriStripSet::BindLateLoads (const ListOfLateLoads& theLateLoads)
{
  myLateLoads = VectorOfLateLoads::FromCollection (theLateLoads);
}
