// JT format reading and visualization tools
// Copyright (C) 2014-2015 OPEN CASCADE SAS
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
// see third_party/occt-jt/MODIFICATIONS.md and PROVENANCE.json for provenance.

#include <JtDecode_VertexData_Quantized.hxx>
#include <JtData_VectorRef.hxx>

Standard_Boolean JtDecode_VertexData_Quantized::UniformQuantizerData::Read (JtData_Reader& theReader)
{
  return theReader.ReadF32 (min) && theReader.ReadF32 (max) && theReader.ReadU8 (bits);
}

Standard_Boolean JtDecode_VertexData_Quantized::PointQuantizerData::Read (JtData_Reader& theReader)
{
  return X.Read (theReader) && Y.Read (theReader) && Z.Read (theReader);
}

JtDecode_VertexData_Quantized::JtDecode_VertexData_Quantized (
                              const PointQuantizerData& theQuantizerData,
                              const Standard_Size       theNbComponents,
                                    JtDecode_Unpack&    theUnpacker)
  : JtDecode_VertexData (theNbComponents, theUnpacker)
  , myQuantizerData     (theQuantizerData) {}

Standard_Integer JtDecode_VertexData_Quantized::getOutCompCount (Standard_Size thePackageCount)
{
  return static_cast<Standard_Integer>(thePackageCount);
}

void JtDecode_VertexData_Quantized::decode (Decoded::Ref theResults)
{
  for (Decoded::CompCountType j = 0; j < theResults.CompCount(); j++)
  {
    Jt_VecI32 aCodes (decodePackage (j));

    const UniformQuantizerData& aCurQuantizerData =
      JtData_VectorRef<const UniformQuantizerData> (myQuantizerData)[j];

    Standard_Real    aMin  = aCurQuantizerData.min;
    Standard_Real    aMax  = aCurQuantizerData.max;
    Standard_Integer aBits = aCurQuantizerData.bits;

    Standard_Real aMaxCode = aBits < 32 ? (1 << aBits) : 0xFFFFFFFF;
    Standard_Real aDecodeMultiplier = (aMax - aMin) / aMaxCode;

    for (Decoded::SizeType i = 0; i < theResults.Count(); i++)
    {
      Standard_Real aValue = aMin + (static_cast<Jt_U32>(aCodes[i]) - 0.5) * aDecodeMultiplier;
      theResults[i][j] = static_cast <float> (aValue);
    }
  }
}
