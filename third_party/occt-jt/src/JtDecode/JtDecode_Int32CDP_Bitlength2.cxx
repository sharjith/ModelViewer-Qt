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
// see third_party/occt-jt/MODIFICATIONS.md and PROVENANCE.json for provenance.

#include <JtDecode_Int32CDP_Bitlength2.hxx>
#include <JtData_Limits.hxx>

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
  if(aMax<aMin) throw std::runtime_error("Invalid JT Mk.2 bitlength range");
  uint64_t aRange = uint64_t(int64_t(aMax)-aMin);
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
      *theResultPtr++ = int32_t(ReadU32 (aFieldWidth) + uint32_t(aMin));
  }
}

void JtDecode_Int32CDP_Bitlength2::decodeVariableWidth (int32_t* theResultPtr, int32_t* theResultEnd)
{
  // Read parameters
  int32_t  aMean         = ReadI32 (32);  // mean value
  unsigned aChgWidthBits = ReadU32 (3);
  unsigned aRunLenBits   = ReadU32 (3);

  if(!aChgWidthBits || !aRunLenBits) throw std::runtime_error("Invalid JT Mk.2 bitlength control widths");

  signed aMaxDecr = -(1 << (aChgWidthBits - 1));     // -ve number
  signed aMaxIncr =  (1 << (aChgWidthBits - 1)) - 1; // +ve number

  // Set initial field width
  int aFieldWidth = 0;

  size_t steps=0;
  const size_t maxSteps=size_t(theResultEnd-theResultPtr)*64+1024;
  // Decode the data
  while (theResultPtr < theResultEnd)
  {
    // adjust field width
    signed aChangeWidth;
    do
    {
      if(++steps>maxSteps) throw std::runtime_error("JT Mk.2 bitlength step limit exceeded");
      if((steps&1023)==0) JtData_Limits::poll();
      aChangeWidth = ReadI32 (aChgWidthBits);
      aFieldWidth += aChangeWidth;
      if(aFieldWidth<0 || aFieldWidth>32) throw std::runtime_error("Invalid JT Mk.2 bitlength field width");
    }
    while (aChangeWidth == aMaxDecr || aChangeWidth == aMaxIncr);

    // read the run length
    uint32_t aRunLen = ReadU32 (aRunLenBits);
    if(!aRunLen || aRunLen>size_t(theResultEnd-theResultPtr)) throw std::runtime_error("Invalid JT Mk.2 bitlength run length");
    int32_t* aRunEnd = theResultPtr + aRunLen;

    // check if the field width is positive
    if (aFieldWidth > 0)
    {
      // if yes, read data bits for the run
      while (theResultPtr < aRunEnd)
        *theResultPtr++ = int32_t(uint32_t(ReadI32 (unsigned(aFieldWidth))) + uint32_t(aMean));
    }
    else
    {
      // otherwise just use the mean value for the whole run
      while (theResultPtr < aRunEnd)
        *theResultPtr++ = aMean;
    }
  }
}
