// Original ModelViewer-Qt file (C3, 2026-11-05), following the structure of the neighbouring OCCT-derived element and node classes.
// Layout derived from NX-written JT 10.10 samples; see research/jt/JT_C3_POLYLINE_POINT_DECODER.md.

#ifndef _JtNode_Shape_PolylineSet_HeaderFile
#define _JtNode_Shape_PolylineSet_HeaderFile

#include <JtNode_Shape_Vertex.hxx>

class JtNode_Shape_PolylineSet : public JtNode_Shape_Vertex
{
public:
  //! Read this entity from a JT file.
  Standard_EXPORT virtual Standard_Boolean Read (JtData_Reader& theReader);

  //! Dump this entity.
  Standard_EXPORT virtual Standard_Integer Dump (Standard_OStream& theStream) const;

  //! Bind a late loaded property to the object.
  Standard_EXPORT virtual void BindLateLoads (const ListOfLateLoads& theLateLoads);

  //! Area factor stored after the vertex shape node data.
  Standard_Real AreaFactor() const { return myAreaFactor; }

  DEFINE_STANDARD_RTTIEXT(JtNode_Shape_PolylineSet, JtNode_Shape_Vertex)
  DEFINE_OBJECT_CLASS (JtNode_Shape_PolylineSet)

private:
  Standard_Real myAreaFactor = 0.0;
};

DEFINE_STANDARD_HANDLE(JtNode_Shape_PolylineSet, JtNode_Shape_Vertex)

#endif // _JtNode_Shape_PolylineSet_HeaderFile
