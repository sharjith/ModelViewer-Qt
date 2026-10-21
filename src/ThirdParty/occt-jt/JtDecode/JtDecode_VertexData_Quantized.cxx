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
// Original rounded topological-coordinate inverse quantization: 2026-10-21.
// Changes: modern OCCT compatibility, JT 10.0 decoding and bounded input handling;
// see src/ThirdParty/occt-jt/MODIFICATIONS.md and PROVENANCE.json for provenance.

#include <JtDecode_VertexData_Quantized.hxx>
#include <JtData_VectorRef.hxx>
#include <cmath>

Standard_Boolean JtDecode_VertexData_Quantized::UniformQuantizerData::Read (JtData_Reader& theReader)
{
  if(!theReader.ReadF32(min) || !theReader.ReadF32(max) || !theReader.ReadU8(bits)) return Standard_False;
  if(!std::isfinite(min) || !std::isfinite(max) || min>max || bits>32)
    throw std::runtime_error("Invalid JT uniform quantizer");
  return Standard_True;
}

Standard_Boolean JtDecode_VertexData_Quantized::PointQuantizerData::Read (JtData_Reader& theReader)
{
  return X.Read (theReader) && Y.Read (theReader) && Z.Read (theReader);
}

JtDecode_VertexData_Quantized::JtDecode_VertexData_Quantized (
                              const PointQuantizerData& theQuantizerData,
                              const Standard_Size       theNbComponents,
                                    JtDecode_Unpack&    theUnpacker,
                              const bool theRoundToNearest)
  : JtDecode_VertexData (theNbComponents, theUnpacker)
  , myQuantizerData     (theQuantizerData)
  , myRoundToNearest    (theRoundToNearest) {}

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

    // JT 9.5 section 8.2.1 / JT 10.0 section 13.2.1 use rounded codes
    // in [0,2^bits-1]. Keep the separately qualified legacy midpoint path.
    if(myRoundToNearest && !aBits && aMin!=aMax)
      throw std::runtime_error("Unsupported nonconstant zero-bit JT coordinate quantizer");
    Standard_Real aMaxCode = aBits < 32 ? std::ldexp(1.0,aBits)-(myRoundToNearest?1:0) : 0xFFFFFFFFu;
    Standard_Real aDecodeMultiplier = aMaxCode ? (aMax-aMin)/aMaxCode : 0;

    for (Decoded::SizeType i = 0; i < theResults.Count(); i++)
    {
      if((i&1023)==0) JtData_Limits::poll();
      if(double(uint32_t(aCodes[i]))>aMaxCode) throw std::runtime_error("JT coordinate quantizer code out of range");
      Standard_Real aValue = aMin + (double(static_cast<Jt_U32>(aCodes[i]))-(myRoundToNearest?0:0.5))*aDecodeMultiplier;
      theResults[i][j] = static_cast <float> (aValue);
    }
  }
}
