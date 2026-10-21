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

#include <JtDecode_Int32CDP_Null.hxx>

int32_t JtDecode_Int32CDP_Null::GetOutValCount() const
{
  return myCodeText.Count();
}

JtDecode_Int32CDP_Null::Decoded::Mover JtDecode_Int32CDP_Null::Decode()
{
  union{
    Jt_VecU32* myUIntData;
    Jt_VecI32* myIntData;
  } aConv;

  aConv.myUIntData = &myCodeText;
  return aConv.myIntData->Move();
}
