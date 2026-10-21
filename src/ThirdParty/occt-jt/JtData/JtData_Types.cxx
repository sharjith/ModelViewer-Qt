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
// see src/ThirdParty/occt-jt/MODIFICATIONS.md and PROVENANCE.json for provenance.

#include <JtData_Types.hxx>

#include <stdio.h>

// =======================================================================
// function : Jt_GUID
// purpose  :
// =======================================================================
Jt_GUID::Jt_GUID (Jt_U32 theU32, Jt_U16 theU16_1, Jt_U16 theU16_2,
                  Jt_U8 theU8_1, Jt_U8 theU8_2, Jt_U8 theU8_3, Jt_U8 theU8_4,
                  Jt_U8 theU8_5, Jt_U8 theU8_6, Jt_U8 theU8_7, Jt_U8 theU8_8)
{
  data.codes.U32    = theU32;
  data.codes.U16[0] = theU16_1;
  data.codes.U16[1] = theU16_2;
  data.codes.U8[0]  = theU8_1;
  data.codes.U8[1]  = theU8_2;
  data.codes.U8[2]  = theU8_3;
  data.codes.U8[3]  = theU8_4;
  data.codes.U8[4]  = theU8_5;
  data.codes.U8[5]  = theU8_6;
  data.codes.U8[6]  = theU8_7;
  data.codes.U8[7]  = theU8_8;
}

// =======================================================================
// function : Jt_GUID
// purpose  :
// =======================================================================
Jt_GUID::Jt_GUID (const char* theString)
{
  Jt_U32 aFields[11];
  sscanf (theString, "%x-%x-%x-%x-%x-%x-%x-%x-%x-%x-%x",
          &aFields[0], &aFields[1], &aFields[2], &aFields[3],
          &aFields[4], &aFields[5], &aFields[6], &aFields[7],
          &aFields[8], &aFields[9], &aFields[10]);

  data.codes.U32    =          aFields[0];
  data.codes.U16[0] = (Jt_U16) aFields[1];
  data.codes.U16[1] = (Jt_U16) aFields[2];
  for (int i = 0; i < 8; i++)
    data.codes.U8[i] = (Jt_U8) aFields[i + 3];
}

// =======================================================================
// function : ToString
// purpose  :
// =======================================================================
Standard_Boolean Jt_GUID::ToString (char* theString) const
{
  return GUIDStringLength ==
    sprintf (theString, "%08x-%04x-%04x-%02x-%02x-%02x-%02x-%02x-%02x-%02x-%02x",
      data.codes.U32, (Jt_U32) data.codes.U16[0], (Jt_U32) data.codes.U16[1],
      (Jt_U32) data.codes.U8[0], (Jt_U32) data.codes.U8[1], (Jt_U32) data.codes.U8[2], (Jt_U32) data.codes.U8[3],
      (Jt_U32) data.codes.U8[4], (Jt_U32) data.codes.U8[5], (Jt_U32) data.codes.U8[6], (Jt_U32) data.codes.U8[7]);
}
