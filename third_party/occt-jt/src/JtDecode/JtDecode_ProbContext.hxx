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

#ifndef _JtDecode_ProbContext_HeaderFile
#define _JtDecode_ProbContext_HeaderFile

#include <JtData_Types.hxx>
#include <JtData_Vector.hxx>
#include <JtDecode_BitReader.hxx>

#include <Standard.hxx>
#include <NCollection_DataMap.hxx>


//! Class representing the probability context for codecs.
//! Mark I and Mark II contexts are supported.
class JtDecode_ProbContextI32
{
public:

  //! The probability context entry.
  struct Entry
  {
    int32_t  symbol;          //!< Symbol
    uint32_t occCount;        //!< Number of occurrences
    int32_t  associatedValue; //!< Value associated with this symbol
    uint32_t nextContext;     //!< Next context if this symbol seen
  };

  //! Read the first probability table for Mark I codec.
  Standard_Boolean ReadFirst (JtDecode_BitReader::Raw& theReader);

  //! Read the next probability table for Mark I codec.
  Standard_Boolean ReadNext  (JtDecode_BitReader::Raw& theReader,
                              const JtDecode_ProbContextI32& theFirstContext);

  //! Read the probability table for Mark II codec.
  Standard_Boolean ReadMk2   (JtDecode_BitReader::Raw& theReader);
  Standard_Boolean ReadMk3   (JtDecode_BitReader::Raw& theReader);

  //! Fetch the entry.
  const Entry& operator[] (Standard_Size theIndex) const { return myEntries[theIndex]; }

  //! Return number of entries.
  Standard_Size Size()  const { return myEntries.Count(); }

  //! Return total number of symbols.
  unsigned TotalCount() const { return myTotalCount; }

protected:

  //! Method reading arbitrary probability table defined by parameters.
  Standard_Boolean read (JtDecode_BitReader::Raw& theReader,
                         const unsigned theEntryCountWidth,
                         const unsigned theSymBitsNumWidth,
                         const unsigned theOccBitsNumWidth,
                         const unsigned theValBitsNumWidth,
                         const unsigned theNxtBitsNumWidth,
                         const unsigned theMinValueWidth,
                         const bool theAllowZeroFrequency = false);

  JtData_Vector<Entry> myEntries;
  unsigned             myTotalCount;
};

#endif
