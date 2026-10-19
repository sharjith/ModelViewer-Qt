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

#include <JtDecode_ProbContext.hxx>

Standard_Boolean JtDecode_ProbContextI32::ReadFirst (JtDecode_BitReader::Raw& theReader)
{
  return read (theReader, 32, 6, 6, 6, 6, 32);
}

Standard_Boolean JtDecode_ProbContextI32::ReadNext (JtDecode_BitReader::Raw& theReader,
                                                    const JtDecode_ProbContextI32& theFirstContext)
{
  if (!read (theReader, 32, 6, 6, 0, 6, 0))
    return Standard_False;

  NCollection_DataMap<Standard_Integer, int32_t> aValuesMap;
  Standard_Size i;
  for (i = 0; i < theFirstContext.Size(); i++)
    aValuesMap.Bind (theFirstContext[i].symbol, theFirstContext[i].associatedValue);
  for (i = 0; i < Size(); i++)
    aValuesMap.Find (myEntries[i].symbol, myEntries[i].associatedValue);

  return Standard_True;
}

Standard_Boolean JtDecode_ProbContextI32::ReadMk2 (JtDecode_BitReader::Raw& theReader)
{
  return read (theReader, 16, 6, 6, 6, 0, 32);
}

Standard_Boolean JtDecode_ProbContextI32::ReadMk3(JtDecode_BitReader::Raw& reader)
{
  if(!reader.Load(61)) return false;
  const auto count=reader.ReadU32(16), occBits=reader.ReadU32(6), valBits=reader.ReadU32(7);
  const int32_t minimum=reader.ReadI32(32);
  if(!count || occBits>32 || valBits>32 || !reader.Load(count*(1+occBits+valBits))) return false;
  myEntries.Allocate(count); myTotalCount=0;
  for(unsigned i=0;i<count;++i) {
    auto& e=myEntries[i];
    e.symbol=reader.ReadBit() ? -2 : int32_t(i);
    e.occCount=reader.ReadU32Or0(occBits);
    e.associatedValue=int32_t(reader.ReadU32Or0(valBits)+uint32_t(minimum));
    e.nextContext=0; myTotalCount+=e.occCount;
  }
  return myTotalCount>0;
}

Standard_Boolean JtDecode_ProbContextI32::read (JtDecode_BitReader::Raw& theReader,
                                                const unsigned theEntryCountWidth,
                                                const unsigned theSymBitsNumWidth,
                                                const unsigned theOccBitsNumWidth,
                                                const unsigned theValBitsNumWidth,
                                                const unsigned theNxtBitsNumWidth,
                                                const unsigned theMinValueWidth)
{
  if (!theReader.Load (theEntryCountWidth + theSymBitsNumWidth + theOccBitsNumWidth
                     + theValBitsNumWidth + theNxtBitsNumWidth + theMinValueWidth))
    return Standard_False;

  unsigned      anEntryCount = theReader.ReadU32Or0 (theEntryCountWidth);
  unsigned      aSymBits     = theReader.ReadU32Or0 (theSymBitsNumWidth);
  unsigned      aOccBits     = theReader.ReadU32Or0 (theOccBitsNumWidth);
  unsigned      aValBits     = theReader.ReadU32Or0 (theValBitsNumWidth);
  unsigned      aNxtBits     = theReader.ReadU32Or0 (theNxtBitsNumWidth);
  int32_t       aMinValue    = theReader.ReadI32Or0 (theMinValueWidth);

  if (!theReader.Load (anEntryCount * (aSymBits + aOccBits + aValBits + aNxtBits)))
    return Standard_False;

  myEntries.Allocate (anEntryCount);
  myTotalCount = 0;

  for (Standard_Size i = 0; i < anEntryCount; i++)
  {
    Entry& anEntry = myEntries[i];

    anEntry.symbol          = theReader.ReadU32Or0 (aSymBits) - 2;
    anEntry.occCount        = theReader.ReadU32Or0 (aOccBits);
    anEntry.associatedValue = theReader.ReadU32Or0 (aValBits) + aMinValue;
    anEntry.nextContext     = theReader.ReadU32Or0 (aNxtBits);

    myTotalCount += anEntry.occCount;
  }

  return Standard_True;
}
