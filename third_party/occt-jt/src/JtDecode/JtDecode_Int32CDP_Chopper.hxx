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

#ifndef _JtDecode_Int32CDP_Chopper_HeaderFile
#define _JtDecode_Int32CDP_Chopper_HeaderFile

#include <JtDecode_Int32CDP.hxx>

//! Chopper decoder implementation.
class JtDecode_Int32CDP_Chopper : public JtDecode_Int32CDP::EncodedData
{
public:
  //! Initialize.
  JtDecode_Int32CDP_Chopper (JtDecode_Int32CDP theMSBData,
                             JtDecode_Int32CDP theLSBData,
                             Jt_U8 theChop, Jt_U8 theSpan, Jt_I32 theBias)
    : myMSBData (theMSBData)
    , myLSBData (theLSBData)
    , myShift   (theSpan - theChop)
    , myBias    (theBias) {}

  //! Get expected count of output values.
  Standard_EXPORT virtual int32_t GetOutValCount() const;

  //! Decode the loaded data.
  Standard_EXPORT virtual Decoded::Mover Decode();

private:
  JtDecode_Int32CDP myMSBData;
  JtDecode_Int32CDP myLSBData;
  Jt_U8  myShift;
  Jt_I32 myBias;
};

#endif
