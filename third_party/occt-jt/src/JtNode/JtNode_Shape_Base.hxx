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

#ifndef _JtNode_Shape_Base_HeaderFile
#define _JtNode_Shape_Base_HeaderFile

#include <JtNode_Base.hxx>
#include <JtProperty_LateLoaded.hxx>

//! Base Shape Node Element is the general form of a shape node that existing in the LSG.
class JtNode_Shape_Base : public JtNode_Base
{
public:
  //! Read this entity from a JT file.
  Standard_EXPORT virtual Standard_Boolean Read (JtData_Reader& theReader);

  //! Dump this entity.
  Standard_EXPORT virtual Standard_Integer Dump (Standard_OStream& theStream) const;

  const Jt_BBoxF32& Bounds() const { return myUntransBnd; }

  //! Get the late loaded properties associated with this node.
  const VectorOfLateLoads& LateLoads() const { return myLateLoads; }

  DEFINE_STANDARD_RTTIEXT(JtNode_Shape_Base, JtNode_Base)
  DEFINE_OBJECT_CLASS (JtNode_Shape_Base)

protected:
  VectorOfLateLoads myLateLoads;

  Jt_BBoxF32 myReservedBnd;
  Jt_BBoxF32 myUntransBnd;
  Jt_F32     myArea;
  Jt_I32     myVertexRange[2];
  Jt_I32     myNodeRange[2];
  Jt_I32     myPolyRange[2];
  Jt_I32     mySize;
  Jt_F32     myCompression;
};

DEFINE_STANDARD_HANDLE(JtNode_Shape_Base, JtNode_Base)

#endif // _JtNode_Shape_Base_HeaderFile
