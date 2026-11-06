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
// Declared CDP1 arithmetic symbol/value count agreement: 2026-10-21.
// Changes: modern OCCT compatibility, JT 10.0 decoding and bounded input handling;
// see src/ThirdParty/occt-jt/MODIFICATIONS.md and PROVENANCE.json for provenance.

#include <JtDecode_Int32CDP_Arithmetic.hxx>
#include <JtData_Limits.hxx>

void JtDecode_Int32CDP_Arithmetic::decode (int32_t* theResultPtr,
                                           int32_t* theResultEnd,
                                     const int32_t* theOOBDataPtr)
{
  // Initialize
  Standard_Size aCurContextIdx = 0;

  uint16_t aLow  = 0x0000;
  uint16_t aHigh = 0xFFFF;

  // Read first symbol
  uint16_t aCode = static_cast <uint16_t> (ReadU32 (16));
  size_t steps=0;
  const bool counted=mySymbolCount>=0;
  const size_t maxSteps=counted?size_t(mySymbolCount):size_t(theResultEnd-theResultPtr)*32+1024;

  // Decode the data
  while (counted ? steps<maxSteps : theResultPtr<theResultEnd)
  {
    if(++steps>maxSteps) throw std::runtime_error("JT arithmetic symbol step limit exceeded");
    if((steps&1023)==0) JtData_Limits::poll();
    if(aCurContextIdx>=myProbContexts.Count() || aCode<aLow || aCode>aHigh)
      throw std::runtime_error("Invalid JT arithmetic context/range");
    // lookup actual context entry
    const JtDecode_ProbContextI32& aCurContext = myProbContexts[aCurContextIdx];
    unsigned aSymbolsInCurCtx = aCurContext.TotalCount();
    if(!aSymbolsInCurCtx || aSymbolsInCurCtx>65536) throw std::runtime_error("Invalid JT arithmetic frequency total");

    unsigned aRange = aHigh - aLow + 1;
    unsigned aCount = unsigned((uint64_t(aCode-aLow+1)*aSymbolsInCurCtx-1)/aRange);

    const JtDecode_ProbContextI32::Entry* aCurEntryPtr = &aCurContext[0];
    unsigned aSymHigh = aCurEntryPtr->occCount;
    while (aSymHigh <= aCount)
    {
      aCurEntryPtr++;
      if(aCurEntryPtr>=&aCurContext[0]+aCurContext.Size()) throw std::runtime_error("Invalid JT arithmetic symbol");
      aSymHigh += aCurEntryPtr->occCount;
    }
    unsigned aSymLow = aSymHigh - aCurEntryPtr->occCount;

    // A later-context escape changes state but does not emit an OOB value.
    // Count it even after the final output value, as CDP1 explicitly declares.
    if((aCurEntryPtr->symbol!=-2 || aCurContextIdx==0) && theResultPtr==theResultEnd)
      throw std::runtime_error("JT arithmetic output count overflow");
    // write output value
    if (aCurEntryPtr->symbol != -2)
      *theResultPtr++ = aCurEntryPtr->associatedValue;
    else if (aCurContextIdx == 0)
    {
      if(!theOOBDataPtr || !myOOBEnd || theOOBDataPtr>=myOOBEnd) throw std::runtime_error("JT out-of-band value underflow");
      *theResultPtr++ = *theOOBDataPtr++;
    }

    // update current context index
    aCurContextIdx = aCurEntryPtr->nextContext;

    // read next symbol

    // expand the range to account for the symbol removal
    aHigh = uint16_t(aLow + uint64_t(aRange)*aSymHigh/aSymbolsInCurCtx - 1);
    aLow = uint16_t(aLow + uint64_t(aRange)*aSymLow/aSymbolsInCurCtx);

    // shift out matched most significant bits
    uint16_t aMask = aHigh ^ aLow;
    if ((aMask & 0x8000) == 0)
    {
      unsigned aShift = 1;
      for (aMask = ~aMask; aMask & 0x4000; aMask <<= 1)
        aShift++;

      aLow  <<= aShift;
      aHigh   = ~(~aHigh << aShift);
      aCode <<= aShift;
      aCode  |= ReadU32 (aShift);
    }

    // check for underflow: 2nd MSB of aHigh is 0 and 2nd MSB of aLow is 1
    // (aHigh = 10xx and aLow = 01xx)
    if ((aHigh & 0x4000) == 0 && (aLow & 0x4000) != 0)
    {
      // underflow is threatening, shift out 2nd most significant bit until it is recovered from
      unsigned aShift = 1;
      for (aMask = 0x2000; (aHigh & aMask) == 0 && (aLow & aMask) != 0; aMask >>= 1)
        aShift++;

      aLow  <<= aShift;
      aHigh   = ~(~aHigh << aShift);
      aCode <<= aShift;
      aCode  |= ReadU32 (aShift);

      aLow  &= 0x7FFF;
      aHigh |= 0x8000;
      aCode ^= 0x8000;
    }
  }
  if(counted && theResultPtr!=theResultEnd)
    throw std::runtime_error("JT arithmetic output count mismatch");
  if(counted && theOOBDataPtr!=myOOBEnd)
    throw std::runtime_error("JT arithmetic unused out-of-band values");
}
