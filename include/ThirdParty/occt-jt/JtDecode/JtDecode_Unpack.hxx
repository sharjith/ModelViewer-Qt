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

#ifndef _JtDecode_Unpack_HeaderFile
#define _JtDecode_Unpack_HeaderFile

#include <JtData_Types.hxx>

typedef void JtDecode_Unpack (JtData_VectorRef<int32_t, int32_t> theValues);

JtDecode_Unpack
  JtDecode_Unpack_Null,
  JtDecode_Unpack_Lag1,
  JtDecode_Unpack_Lag2,
  JtDecode_Unpack_Xor1,
  JtDecode_Unpack_Xor2,
  JtDecode_Unpack_Ramp,
  JtDecode_Unpack_Stride1,
  JtDecode_Unpack_Stride2,
  JtDecode_Unpack_StripIdx;

#endif
