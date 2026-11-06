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
// JT 9.5 qualification and shared attribute/codec bounds updated: 2026-10-20.
// Changes: modern OCCT compatibility, JT 10.0 decoding and bounded input handling;
// see src/ThirdParty/occt-jt/MODIFICATIONS.md and PROVENANCE.json for provenance.

#include <JtDecode_Int32CDP_Chopper.hxx>
#include <JtData_Limits.hxx>

int32_t JtDecode_Int32CDP_Chopper::GetOutValCount() const
{
  return myMSBData.GetOutValCount();
}

JtDecode_Int32CDP_Chopper::Decoded::Mover JtDecode_Int32CDP_Chopper::Decode()
{
  // Decode chopped MSB and LSB Data
  Decoded aMSBVec (decodePackage (myMSBData));
  Decoded aLSBVec (decodePackage (myLSBData));

  // Decode the results
  Decoded aResults (aMSBVec.Count());
  for (int32_t i = 0; i < aMSBVec.Count(); i++)
  {
    if((i&1023)==0) JtData_Limits::poll();
    if(myShift<32 && (uint32_t(aLSBVec[i])>>myShift)!=0) throw std::runtime_error("Invalid JT chopper low field");
    aResults[i] = int32_t(((uint32_t(aMSBVec[i]) << myShift) | uint32_t(aLSBVec[i])) + uint32_t(myBias));
  }

  return aResults.Move();
}
