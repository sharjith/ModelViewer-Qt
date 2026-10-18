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

#include <JtDecode_Int32CDP_Bitlength.hxx>

void JtDecode_Int32CDP_Bitlength::decode (int32_t* theResultPtr, int32_t* theResultEnd)
{
  // Set initial field width
  unsigned aFieldWidth = 0;

  // Decode the data
  while (theResultPtr < theResultEnd)
  {
    // read prefix
    if (ReadBit())
    {
      uint32_t aFirstBit = ReadBit();
      signed aStep = aFirstBit ? STEP_SIZE : -STEP_SIZE;
      do aFieldWidth += aStep;
      while (ReadBit() == aFirstBit);
    }

    // read value
    *theResultPtr++ = ReadI32Or0 (aFieldWidth);
  }
}
