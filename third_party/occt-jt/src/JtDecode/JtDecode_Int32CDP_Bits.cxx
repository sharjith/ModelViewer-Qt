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

#include <JtDecode_Int32CDP_Bits.hxx>

int32_t JtDecode_Int32CDP_Bits::GetOutValCount() const
{
  return myOutValCount;
}

JtDecode_Int32CDP_Bits::Decoded::Mover JtDecode_Int32CDP_Bits::Decode()
{
  // Decode the loaded bits into the memory
  Decoded aResults (myOutValCount);
  decode (aResults.Data(), aResults.Data() + myOutValCount);
  return aResults.Move();
}

void JtDecode_Int32CDP_Bits::decode (int32_t* theResultPtr, int32_t* theResultEnd)
{
  // Decode out-of-band data
  Decoded anOutOfBandValues (decodePackage (myOOBData));
  myOOBEnd=anOutOfBandValues.Count() ? anOutOfBandValues.Data()+anOutOfBandValues.Count() : nullptr;

  // Decode the loaded bits using the out-of-band values
  decode (theResultPtr, theResultEnd, anOutOfBandValues.Data());
}

void JtDecode_Int32CDP_Bits::decode (int32_t*, int32_t*, const int32_t*) {}
