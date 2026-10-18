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

#include <JtNode_Shape_Vertex.hxx>
#include <JtData_Reader.hxx>

#include <JtElement_ShapeLOD_Vertex.hxx>

IMPLEMENT_STANDARD_HANDLE (JtNode_Shape_Vertex, JtNode_Shape_Base)
IMPLEMENT_STANDARD_RTTIEXT(JtNode_Shape_Vertex, JtNode_Shape_Base)

IMPLEMENT_OBJECT_CLASS(JtNode_Shape_Vertex, "Vertex Shape Object",
                       "10dd107f-2ac8-11d1-6b-9b-00-80-c7-bb-59-97")

//=======================================================================
//function : Read
//purpose  : Read this entity from a translate file
//=======================================================================
Standard_Boolean JtNode_Shape_Vertex::Read (JtData_Reader &theReader)
{
  if (!JtNode_Shape_Base::Read (theReader))
    return Standard_False;

  Jt_I16 aVersion = 1;

  if (theReader.Model()->MajorVersion() < 9)
  {
    Jt_I32 aNormalBinding;
    Jt_I32 aTextCoordBinding;
    Jt_I32 aColorBinding;
    if (!theReader.ReadI32 (aNormalBinding)
     || !theReader.ReadI32 (aTextCoordBinding)
     || !theReader.ReadI32 (aColorBinding))
      return Standard_False;
  }
  else
  {
    JtElement_ShapeLOD_Vertex::VertexBinding2 aVertexBinding;
    if (!theReader.ReadVersion(aVersion)
     || !aVertexBinding.Read (theReader))
      return Standard_False;
  }

  JtElement_ShapeLOD_Vertex::QuantizationParams aQuantizationParams;
  if (theReader.Model()->MajorVersion()>=10) return Standard_True;
  if (!aQuantizationParams.Read (theReader))
    return Standard_False;

  if (aVersion != 1)
  {
    JtElement_ShapeLOD_Vertex::VertexBinding2 aVertexBinding;
    if (!aVertexBinding.Read (theReader))
      return Standard_False;
  }

  return Standard_True;
}

//=======================================================================
//function : Dump
//purpose  :
//=======================================================================
Standard_Integer JtNode_Shape_Vertex::Dump (Standard_OStream &theStream) const
{
  theStream << "JtNode_Shape_Vertex ";
  return JtNode_Shape_Base::Dump (theStream);
}
