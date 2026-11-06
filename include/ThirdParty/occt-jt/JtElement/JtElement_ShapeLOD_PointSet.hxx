// Original ModelViewer-Qt file (C3, 2026-11-05), following the structure of the neighbouring OCCT-derived element and node classes.
// Layout derived from NX-written JT 10.10 samples; see research/jt/JT_C3_POLYLINE_POINT_DECODER.md.

#ifndef _JtElement_ShapeLOD_PointSet_HeaderFile
#define _JtElement_ShapeLOD_PointSet_HeaderFile

#include <JtElement_ShapeLOD_Vertex.hxx>

//! Point set shape LOD: independent points with the vertex data of the vertex shape LOD.
class JtElement_ShapeLOD_PointSet : public JtElement_ShapeLOD_Vertex
{
public:
  //! Read this entity from a JT file.
  Standard_EXPORT virtual Standard_Boolean Read (JtData_Reader& theReader);

  //! Dump this entity.
  Standard_EXPORT virtual Standard_Integer Dump (Standard_OStream& theStream) const;

  DEFINE_STANDARD_RTTIEXT(JtElement_ShapeLOD_PointSet, JtElement_ShapeLOD_Vertex)
  DEFINE_OBJECT_CLASS (JtElement_ShapeLOD_PointSet)
};

DEFINE_STANDARD_HANDLE(JtElement_ShapeLOD_PointSet, JtElement_ShapeLOD_Vertex)

#endif // _JtElement_ShapeLOD_PointSet_HeaderFile
