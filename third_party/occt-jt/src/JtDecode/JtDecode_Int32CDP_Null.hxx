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

#ifndef _JtDecode_Int32CDP_Null_HeaderFile
#define _JtDecode_Int32CDP_Null_HeaderFile

#include <JtDecode_Int32CDP.hxx>

//! Null codec provides interface to read data which is not encoded.
class JtDecode_Int32CDP_Null : public JtDecode_Int32CDP::EncodedData
{
public:
  //! Initialize.
  JtDecode_Int32CDP_Null (Jt_VecU32::Mover theCodeText) : myCodeText (theCodeText) {}

  //! Get expected count of output values.
  Standard_EXPORT virtual int32_t GetOutValCount() const;

  //! Move the loaded not encoded data to the given vector.
  Standard_EXPORT virtual Decoded::Mover Decode();

protected:
  Jt_VecU32 myCodeText;
};

#endif
