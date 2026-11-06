// Original ModelViewer-Qt file (C3, 2026-11-05), following the structure of the neighbouring OCCT-derived element and node classes.
// Layout derived from NX-written JT 10.10 samples; see research/jt/JT_C3_POLYLINE_POINT_DECODER.md.

#include <JtNode_Shape_PointSet.hxx>
#include <JtData_Reader.hxx>

IMPLEMENT_STANDARD_HANDLE (JtNode_Shape_PointSet, JtNode_Shape_Vertex)
IMPLEMENT_STANDARD_RTTIEXT(JtNode_Shape_PointSet, JtNode_Shape_Vertex)

IMPLEMENT_OBJECT_CLASS(JtNode_Shape_PointSet, "Point Set Shape Object",
                       "98134716-0010-0818-19-98-08-00-09-83-5d-5a")

Standard_Boolean JtNode_Shape_PointSet::Read (JtData_Reader& theReader)
{
  if (theReader.Model()->MajorVersion() < 9)
    throw std::runtime_error("Unsupported JT 8.x point set shape node");
  Jt_I16 aVersion;
  Jt_F32 anAreaFactor;
  if (!JtNode_Shape_Vertex::Read (theReader)
   || !theReader.ReadVersion (aVersion)
   || !theReader.ReadF32 (anAreaFactor))
    return Standard_False;
  if (aVersion != 1)
    throw std::runtime_error("Unsupported JT point set shape node version");
  myAreaFactor = anAreaFactor;
  return Standard_True;
}

Standard_Integer JtNode_Shape_PointSet::Dump (Standard_OStream& theStream) const
{
  theStream << "JtNode_Shape_PointSet ";
  return JtNode_Shape_Vertex::Dump (theStream);
}

void JtNode_Shape_PointSet::BindLateLoads (const ListOfLateLoads& theLateLoads)
{
  myLateLoads = VectorOfLateLoads::FromCollection (theLateLoads);
}
