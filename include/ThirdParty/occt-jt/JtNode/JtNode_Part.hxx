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

#ifndef _JtNode_Part_HeaderFile
#define _JtNode_Part_HeaderFile

#include <JtNode_MetaData.hxx>
#include <JtProperty_LateLoaded.hxx>

//! Part Node Element represent the particular part in LSG structure.
class JtNode_Part : public JtNode_MetaData
{
public:
  //! Read this entity from a JT file.
  Standard_EXPORT virtual Standard_Boolean Read (JtData_Reader& theReader);

  //! Dump this entity.
  Standard_EXPORT virtual Standard_Integer Dump (Standard_OStream& theStream) const;

  //! Bind a late loaded property to the object.
  Standard_EXPORT virtual void BindLateLoads (const ListOfLateLoads& theLateLoads);

  //! Get the late loaded properties associated with this node.
  const VectorOfLateLoads& LateLoads() const { return myLateLoads; }

  DEFINE_STANDARD_RTTIEXT(JtNode_Part, JtNode_MetaData)
  DEFINE_OBJECT_CLASS (JtNode_Part)

protected:
  VectorOfLateLoads myLateLoads;
};

DEFINE_STANDARD_HANDLE(JtNode_Part, JtNode_MetaData)

#endif // _JtNode_Part_HeaderFile
