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

#ifndef _JtNode_Partition_HeaderFile
#define _JtNode_Partition_HeaderFile

#include <JtNode_Group.hxx>
#include <JtData_Model.hxx>

#include <TCollection_ExtendedString.hxx>

//! Leaf node representing the external JT file reference.
class JtNode_Partition : public JtNode_Group
{
public:
  //! Read this entity from a JT file.
  Standard_EXPORT virtual Standard_Boolean Read (JtData_Reader& theReader);

  //! Dump this entity.
  Standard_EXPORT virtual Standard_Integer Dump (Standard_OStream& theStream) const;

  //! Load the referenced JT file and bind its LSG to this node.
  Standard_EXPORT Standard_Boolean Load();

  //! Unbind children from this node and unload the referenced JT file.
  Standard_EXPORT void Unload();

  //! Returns file name.
  const TCollection_ExtendedString& FileName() const { return myFileName; }
  const TCollection_ExtendedString& OriginFileName() const {return myModel->FileName();}

  DEFINE_STANDARD_RTTIEXT(JtNode_Partition, JtNode_Group)
  DEFINE_OBJECT_CLASS (JtNode_Partition)

protected:
  Handle(JtData_Model)       myModel;
  TCollection_ExtendedString myFileName;
};

DEFINE_STANDARD_HANDLE(JtNode_Partition, JtNode_Group)

#endif // _JtNode_Partition_HeaderFile
