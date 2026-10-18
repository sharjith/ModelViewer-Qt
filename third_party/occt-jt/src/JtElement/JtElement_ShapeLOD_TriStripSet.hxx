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

#ifndef _JtElement_ShapeLOD_TriStripSet_HeaderFile
#define _JtElement_ShapeLOD_TriStripSet_HeaderFile

#include <JtElement_ShapeLOD_Vertex.hxx>

//! A Tri-Strip Set Shape LOD Element holds the geometric shape definition data
//! (tesselation) for a single LOD.
class JtElement_ShapeLOD_TriStripSet : public JtElement_ShapeLOD_Vertex
{
public:
  //! Read this entity from a JT file.
  Standard_EXPORT virtual Standard_Boolean Read (JtData_Reader& theReader);

  //! Dump this entity.
  Standard_EXPORT virtual Standard_Integer Dump (Standard_OStream& theStream) const;

  DEFINE_STANDARD_RTTIEXT(JtElement_ShapeLOD_TriStripSet, JtElement_ShapeLOD_Vertex)
  DEFINE_OBJECT_CLASS (JtElement_ShapeLOD_TriStripSet)
};

DEFINE_STANDARD_HANDLE(JtElement_ShapeLOD_TriStripSet, JtElement_ShapeLOD_Vertex)

#endif // _JtElement_ShapeLOD_TriStripSet_HeaderFile
