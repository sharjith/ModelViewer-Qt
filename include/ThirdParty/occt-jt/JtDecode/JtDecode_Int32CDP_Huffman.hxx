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
// Bounded Huffman code-text tracking: 2026-10-20.
// Changes: modern OCCT compatibility, JT 10.0 decoding and bounded input handling;
// see src/ThirdParty/occt-jt/MODIFICATIONS.md and PROVENANCE.json for provenance.

#ifndef _JtDecode_Int32CDP_Huffman_HeaderFile
#define _JtDecode_Int32CDP_Huffman_HeaderFile

#include <JtDecode_Int32CDP_Bits.hxx>

//! Implementation of Huffman code decoder.
class JtDecode_Int32CDP_Huffman : public JtDecode_Int32CDP_Bits
{
public:

  //! Initialize the huffman dedoder by reader.
  //! @param theReader - current readed to obtain codec parameters and coded data.
  JtDecode_Int32CDP_Huffman (JtData_Reader& theReader) : JtDecode_Int32CDP_Bits (theReader) {}

  void SetCodeTextLength(int32_t bits) {myCodeTextLength=bits;}

protected:

  //! Decode the data.
  Standard_EXPORT virtual void decode (int32_t* theResultPtr,
                                       int32_t* theResultEnd,
                                 const int32_t* theOOBDataPtr);
private:
  int32_t myCodeTextLength=0;
};

#endif
