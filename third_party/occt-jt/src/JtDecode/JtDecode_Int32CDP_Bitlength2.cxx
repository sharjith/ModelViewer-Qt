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

#include <JtDecode_Int32CDP_Bitlength2.hxx>

void JtDecode_Int32CDP_Bitlength2::decode (int32_t* theResultPtr, int32_t* theResultEnd)
{
  // Analyze the variable-width tag
  if (ReadBit())
    decodeVariableWidth (theResultPtr, theResultEnd);
  else
    decodeFixedWidth    (theResultPtr, theResultEnd);
}

void JtDecode_Int32CDP_Bitlength2::decodeFixedWidth (int32_t* theResultPtr, int32_t* theResultEnd)
{
  // Read the min and max symbols from the stream
  unsigned aMinBits = ReadU32 (6); // number of bits in the minimum symbol
  unsigned aMaxBits = ReadU32 (6); // number of bits in the maximum symbol

  int32_t aMin = ReadI32Or0 (aMinBits); // the minimum symbol value - used as bias
  int32_t aMax = ReadI32Or0 (aMaxBits); // the maximum symbol value - used as bias

  // Check if the maximum is greater than the minimum
  int aRange = aMax - aMin;
  if (aRange <= 0)
  {
    // if no, just fill the result vector with the value
    while (theResultPtr < theResultEnd)
      *theResultPtr++ = aMin;
  }
  else
  {
    // calculate field width in bits
    unsigned aFieldWidth = 1;
    for (aRange >>= 1; aRange; aRange >>= 1)
      aFieldWidth++;

    // read each fixed-width field and output the value
    while (theResultPtr < theResultEnd)
      *theResultPtr++ = ReadU32 (aFieldWidth) + aMin;
  }
}

void JtDecode_Int32CDP_Bitlength2::decodeVariableWidth (int32_t* theResultPtr, int32_t* theResultEnd)
{
  // Read parameters
  int32_t  aMean         = ReadI32 (32);  // mean value
  unsigned aChgWidthBits = ReadU32 (3);
  unsigned aRunLenBits   = ReadU32 (3);

  signed aMaxDecr = -(1 << (aChgWidthBits - 1));     // -ve number
  signed aMaxIncr =  (1 << (aChgWidthBits - 1)) - 1; // +ve number

  // Set initial field width
  unsigned aFieldWidth = 0;

  // Decode the data
  while (theResultPtr < theResultEnd)
  {
    // adjust field width
    signed aChangeWidth;
    do
    {
      aChangeWidth = ReadI32 (aChgWidthBits);
      aFieldWidth += aChangeWidth;
    }
    while (aChangeWidth == aMaxDecr || aChangeWidth == aMaxIncr);

    // read the run length
    uint32_t aRunLen = ReadU32 (aRunLenBits);
    int32_t* aRunEnd = theResultPtr + aRunLen;

    // check if the field width is positive
    if (aFieldWidth > 0)
    {
      // if yes, read data bits for the run
      while (theResultPtr < aRunEnd)
        *theResultPtr++ = ReadI32 (aFieldWidth) + aMean;
    }
    else
    {
      // otherwise just use the mean value for the whole run
      while (theResultPtr < aRunEnd)
        *theResultPtr++ = aMean;
    }
  }
}
