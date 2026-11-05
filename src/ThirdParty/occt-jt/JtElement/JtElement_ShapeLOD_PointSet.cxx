// Original ModelViewer-Qt file (C3, 2026-11-05), following the structure of the neighbouring OCCT-derived element and node classes.
// Layout derived from NX-written JT 10.10 samples; see research/jt/JT_C3_POLYLINE_POINT_DECODER.md.

#include <JtElement_ShapeLOD_PointSet.hxx>
#include <JtData_Reader.hxx>

IMPLEMENT_STANDARD_HANDLE (JtElement_ShapeLOD_PointSet, JtElement_ShapeLOD_Vertex)
IMPLEMENT_STANDARD_RTTIEXT(JtElement_ShapeLOD_PointSet, JtElement_ShapeLOD_Vertex)

IMPLEMENT_OBJECT_CLASS(JtElement_ShapeLOD_PointSet, "Point Set Shape LOD Object",
                       "98134716-0011-0818-19-98-08-00-09-83-5d-5a")

Standard_Boolean JtElement_ShapeLOD_PointSet::Read (JtData_Reader& theReader)
{
  if (theReader.Model()->MajorVersion() < 9)
    throw std::runtime_error("Unsupported JT 8.x point set shape LOD");

  Jt_I16 aVersion;
  if (!readVertexShapeLODData (theReader, Standard_False, PrimitiveKind::Points)
   || !theReader.ReadVersion(aVersion))
    return Standard_False;
  if (aVersion != 1)
    throw std::runtime_error("Unsupported JT point set shape LOD version");
  return Standard_True;
}

Standard_Integer JtElement_ShapeLOD_PointSet::Dump (Standard_OStream& theStream) const
{
  theStream << "JtElement_ShapeLOD_PointSet ";
  return JtElement_ShapeLOD_Vertex::Dump (theStream);
}
