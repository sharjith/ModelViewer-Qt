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

#ifndef _JtNode_RangeLOD_HeaderFile
#define _JtNode_RangeLOD_HeaderFile

#include <JtNode_LOD.hxx>

//! Range LOD Node holds a list of alternate representations and the ranges
//! on which those representations are applicable.
class JtNode_RangeLOD : public JtNode_LOD
{
public:
  //! Read this entity from a JT file.
  Standard_EXPORT virtual Standard_Boolean Read (JtData_Reader& theReader);

  //! Dump this entity.
  Standard_EXPORT virtual Standard_Integer Dump (Standard_OStream& theStream) const;

  const Jt_VecF32& RangeLimits() const { return myVecR; }
  const Jt_CoordF32& Center()    const { return myCenter; }

  DEFINE_STANDARD_RTTIEXT(JtNode_RangeLOD, JtNode_LOD)
  DEFINE_OBJECT_CLASS (JtNode_RangeLOD)

protected:
  Jt_I16      myVersion;
  Jt_VecF32   myVecR;
  Jt_CoordF32 myCenter;
};

DEFINE_STANDARD_HANDLE(JtNode_RangeLOD, JtNode_LOD)

#endif // _JtNode_RangeLOD_HeaderFile
