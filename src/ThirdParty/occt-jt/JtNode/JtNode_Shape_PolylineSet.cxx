// Original ModelViewer-Qt file (C3, 2026-11-05), following the structure of the neighbouring OCCT-derived element and node classes.
// Layout derived from NX-written JT 10.10 samples; see research/jt/JT_C3_POLYLINE_POINT_DECODER.md.

#include <JtNode_Shape_PolylineSet.hxx>
#include <JtData_Reader.hxx>

IMPLEMENT_STANDARD_HANDLE (JtNode_Shape_PolylineSet, JtNode_Shape_Vertex)
IMPLEMENT_STANDARD_RTTIEXT(JtNode_Shape_PolylineSet, JtNode_Shape_Vertex)

IMPLEMENT_OBJECT_CLASS(JtNode_Shape_PolylineSet, "Polyline Set Shape Object",
                       "10dd1046-2ac8-11d1-9b-6b-00-80-c7-bb-59-97")

Standard_Boolean JtNode_Shape_PolylineSet::Read (JtData_Reader& theReader)
{
  if (theReader.Model()->MajorVersion() < 9)
    throw std::runtime_error("Unsupported JT 8.x polyline set shape node");
  Jt_I16 aVersion;
  Jt_F32 anAreaFactor;
  if (!JtNode_Shape_Vertex::Read (theReader)
   || !theReader.ReadVersion (aVersion)
   || !theReader.ReadF32 (anAreaFactor))
    return Standard_False;
  if (aVersion != 1)
    throw std::runtime_error("Unsupported JT polyline set shape node version");
  myAreaFactor = anAreaFactor;
  return Standard_True;
}

Standard_Integer JtNode_Shape_PolylineSet::Dump (Standard_OStream& theStream) const
{
  theStream << "JtNode_Shape_PolylineSet ";
  return JtNode_Shape_Vertex::Dump (theStream);
}

void JtNode_Shape_PolylineSet::BindLateLoads (const ListOfLateLoads& theLateLoads)
{
  myLateLoads = VectorOfLateLoads::FromCollection (theLateLoads);
}
