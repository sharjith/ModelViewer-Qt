// Original ModelViewer-Qt file (C3, 2026-11-05), following the structure of the neighbouring OCCT-derived element and node classes.
// Layout derived from NX-written JT 10.10 samples; see research/jt/JT_C3_POLYLINE_POINT_DECODER.md.

#include <JtElement_ShapeLOD_PolylineSet.hxx>
#include <JtData_Reader.hxx>

IMPLEMENT_STANDARD_HANDLE (JtElement_ShapeLOD_PolylineSet, JtElement_ShapeLOD_Vertex)
IMPLEMENT_STANDARD_RTTIEXT(JtElement_ShapeLOD_PolylineSet, JtElement_ShapeLOD_Vertex)

IMPLEMENT_OBJECT_CLASS(JtElement_ShapeLOD_PolylineSet, "Polyline Set Shape LOD Object",
                       "10dd10a1-2ac8-11d1-9b-6b-00-80-c7-bb-59-97")

Standard_Boolean JtElement_ShapeLOD_PolylineSet::Read (JtData_Reader& theReader)
{
  if (theReader.Model()->MajorVersion() < 9)
    throw std::runtime_error("Unsupported JT 8.x polyline set shape LOD");

  Jt_I16 aVersion;
  if (!readVertexShapeLODData (theReader, Standard_False, PrimitiveKind::Lines)
   || !theReader.ReadVersion(aVersion))
    return Standard_False;
  if (aVersion != 1)
    throw std::runtime_error("Unsupported JT polyline set shape LOD version");
  return Standard_True;
}

Standard_Integer JtElement_ShapeLOD_PolylineSet::Dump (Standard_OStream& theStream) const
{
  theStream << "JtElement_ShapeLOD_PolylineSet ";
  return JtElement_ShapeLOD_Vertex::Dump (theStream);
}
