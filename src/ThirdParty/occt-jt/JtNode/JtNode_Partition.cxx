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

#include <JtNode_Partition.hxx>
#include <JtData_Reader.hxx>

IMPLEMENT_STANDARD_HANDLE (JtNode_Partition, JtNode_Group)
IMPLEMENT_STANDARD_RTTIEXT(JtNode_Partition, JtNode_Group)

IMPLEMENT_OBJECT_CLASS(JtNode_Partition, "Partition Object",
                       "10dd103e-2ac8-11d1-9b-6b-00-80-c7-bb-59-97")

//=======================================================================
//function : Read
//purpose  : Read this entity from a translate file
//=======================================================================
Standard_Boolean JtNode_Partition::Read (JtData_Reader& theReader)
{
  myModel = theReader.Model();

  Jt_U8 version;
  Jt_I32 aFlags;
  if (!JtNode_Group::Read     (theReader)
   || (theReader.Model()->MajorVersion()>=10 && !theReader.ReadU8(version))
   || !theReader.ReadI32      (aFlags)
   || !theReader.ReadMbString (myFileName))
  {
    return Standard_False;
  }

  const Standard_Boolean hasUntransformedBndBox = ((aFlags & 1) != 0);

  Jt_BBoxF32 aBBox;
  if ((theReader.Model()->MajorVersion()<10 || hasUntransformedBndBox)
   && !theReader.ReadUniformStruct<Jt_F32> (aBBox))
    return Standard_False;

  Jt_F32 anArea;
  Jt_I32 aVrtxRange[2];
  Jt_I32 aNodeRange[2];
  Jt_I32 aPolyRange[2];
  if (!theReader.ReadF32   (anArea)
   || !theReader.ReadArray (aVrtxRange)
   || !theReader.ReadArray (aNodeRange)
   || !theReader.ReadArray (aPolyRange))
  {
    return Standard_False;
  }

  if (theReader.Model()->MajorVersion()<10 && hasUntransformedBndBox
   && !theReader.ReadUniformStruct<Jt_F32> (aBBox))
  {
    return Standard_False;
  }

  return Standard_True;
}

//=======================================================================
//function : Dump
//purpose  :
//=======================================================================
Standard_Integer JtNode_Partition::Dump (Standard_OStream& theStream) const
{
  theStream << "JtNode_Partition ";
  return JtNode_Group::Dump (theStream);
}

//=======================================================================
//function : Load
//purpose  : Load the referenced JT file and bind its LSG to this node
//=======================================================================
Standard_Boolean JtNode_Partition::Load()
{
  Handle(JtData_Model)     aModel    = new JtData_Model (myFileName, myModel);
  Handle(JtNode_Partition) aRootNode = aModel->Init();
  if (aRootNode.IsNull())
    return Standard_False;

  myChildren = aRootNode->Children();

  return Standard_True;
}

//=======================================================================
//function : Unload
//purpose  : Unbind children from this node and unload the referenced JT file
//=======================================================================
void JtNode_Partition::Unload()
{
  myChildren.Free();
}
