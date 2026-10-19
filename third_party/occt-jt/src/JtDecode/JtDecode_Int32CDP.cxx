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

#include <JtDecode_Int32CDP.hxx>

#include <JtDecode_BitReader.hxx>
#include <JtDecode_ProbContext.hxx>

#include <JtDecode_Int32CDP_Null.hxx>
#include <JtDecode_Int32CDP_Bitlength.hxx>
#include <JtDecode_Int32CDP_Bitlength2.hxx>
#include <JtDecode_Int32CDP_Huffman.hxx>
#include <JtDecode_Int32CDP_Arithmetic.hxx>
#include <JtDecode_Int32CDP_Chopper.hxx>
#include <JtDecode_Int32CDP_Mtf.hxx>
#include <JtDecode_Int32CDP_Bitlength3.hxx>

Standard_Boolean JtDecode_Int32CDP::LoadPlain(JtData_Reader& reader) {
  Jt_VecU32 values;
  if(!reader.ReadVec(values)) return false;
  myEncodedData=EncodedDataHandle(new JtDecode_Int32CDP_Null(values.Move()));
  return true;
}

#include <JtData_Message.hxx>

JtDecode_Int32CDP::EncodedDataHandle JtDecode_Int32CDP::loadCDP1 (JtData_Reader& theReader)
{
  static thread_local unsigned depth=0;
  if(depth>=8) throw std::runtime_error("JT legacy integer codec recursion limit exceeded");
  struct Depth {unsigned& d;Depth(unsigned& d):d(d){++d;}~Depth(){--d;}} guard(depth);
  // Read codec type
  Standard_Integer aCodec;
  if (!theReader.ReadU8 (aCodec))
    return EncodedDataHandle();

  if(theReader.Model()->MajorVersion()==8 && aCodec!=0 && aCodec!=1 && aCodec!=3)
    throw std::runtime_error("Unsupported JT 8.x integer codec (qualified: null, bitlength and arithmetic)");

  // Null codec, just read uncoded values
  if (aCodec == 0)
  {
    Jt_VecU32 aCodeText;
    if (!theReader.ReadVec (aCodeText))
      return EncodedDataHandle();

    return EncodedDataHandle (new JtDecode_Int32CDP_Null (aCodeText.Move()));
  }

  // Bits based codec
  JtData_SingleHandle<JtDecode_Int32CDP_Bits> aData;
  switch (aCodec)
  {
  case 1:
    aData = new JtDecode_Int32CDP_Bitlength  (theReader);
    break;
  case 2:
    aData = new JtDecode_Int32CDP_Huffman    (theReader);
    break;
  case 3:
    aData = new JtDecode_Int32CDP_Arithmetic (theReader);
    break;
  default:
    TRACE ("Unknown Codec (" + aCodec + ")!");
    return EncodedDataHandle();
  }

  // Huffman or arithmetic codec: read probability contexts and load OOB data
  Standard_Integer aProbContextTableCount = 0;
  if (aCodec >= 2)
  {
    // read probability contexts
    if (!theReader.ReadU8 (aProbContextTableCount))
      return EncodedDataHandle();
    if(aProbContextTableCount<1 || aProbContextTableCount>16)
      throw std::runtime_error("Invalid legacy JT probability context count");

    JtData_Vector<JtDecode_ProbContextI32> aProbContexts (aProbContextTableCount);
    {
      JtDecode_BitReader::Raw aContextReader (theReader);

      if (!aProbContexts[0].ReadFirst (aContextReader))
        return EncodedDataHandle();

      for (Standard_Integer i = 1; i < aProbContextTableCount; i++)
        if (!aProbContexts[i].ReadNext (aContextReader, aProbContexts[0]))
          return EncodedDataHandle();
    }
    for(int table=0;table<aProbContextTableCount;++table)
      for(size_t entry=0;entry<aProbContexts[table].Size();++entry)
        if(aProbContexts[table][entry].nextContext>=unsigned(aProbContextTableCount))
          throw std::runtime_error("Invalid legacy JT next probability context");
    aData->SetProbContexts (aProbContexts.Move());

    // load out-of-band data
    Jt_I32 anOutOfBandValueCount;
    if(!theReader.ReadI32 (anOutOfBandValueCount) || anOutOfBandValueCount<0 || anOutOfBandValueCount>16000000)
      throw std::runtime_error("Invalid legacy JT out-of-band count");
    if (anOutOfBandValueCount > 0)
    {
      JtDecode_Int32CDP anOOBData;
      if (!anOOBData.Load1 (theReader))
        return EncodedDataHandle();
      if(anOOBData.GetOutValCount()!=anOutOfBandValueCount)
        throw std::runtime_error("Legacy JT out-of-band count mismatch");
      aData->SetOOBData (anOOBData);
    }
  }

  // Read CodeText length and number of output values
  Jt_I32 aCodeTextLength;
  Jt_I32 aValueCount;
  if (!theReader.ReadI32 (aCodeTextLength)
   || !theReader.ReadI32 (aValueCount))
  {
    return EncodedDataHandle();
  }
  if(aCodeTextLength<0 || aCodeTextLength>256*1024*1024 || aValueCount<0 || aValueCount>16000000)
    throw std::runtime_error("Invalid legacy JT integer packet count/length");
  aData->SetOutValCount (aValueCount);

  // Read symbol count if present
  if (aProbContextTableCount > 1)
  {
    Jt_I32 aSymbolCount;
    if (!theReader.ReadI32 (aSymbolCount))
      return EncodedDataHandle();
  }

  // Read CodeText data
  Jt_I32 words;
  if(!theReader.ReadI32(words) || words<0 || words!=(aCodeTextLength+31)/32)
    throw std::runtime_error("Invalid legacy JT integer code word count");
  if (!aData->Load(size_t(words)))
    return EncodedDataHandle();

  return EncodedDataHandle (aData);
}

JtDecode_Int32CDP::EncodedDataHandle JtDecode_Int32CDP::loadCDP2 (JtData_Reader& theReader)
{
  static thread_local unsigned depth=0;
  if(depth>=8) throw std::runtime_error("JT integer codec recursion limit exceeded");
  struct Depth {unsigned& d;Depth(unsigned& d):d(d){++d;}~Depth(){--d;}} guard(depth);
  // Read number of output values
  Jt_I32 aValueCount;
  if (!theReader.ReadI32 (aValueCount) || aValueCount<0 || aValueCount>16000000)
    return EncodedDataHandle();

  if (aValueCount == 0)
    return EncodedDataHandle (new JtDecode_Int32CDP_Null (Jt_VecU32().Move()));

  // Read codec type
  Standard_Integer aCodec;
  if (!theReader.ReadU8 (aCodec))
    return EncodedDataHandle();

  // Chopper pseudo-codec
  if(aCodec==5 && theReader.Model()->MajorVersion()>=10) {
    JtDecode_Int32CDP values, offsets;
    if(!values.Load2(theReader) || !offsets.Load2(theReader)) return {};
    return EncodedDataHandle(new JtDecode_Int32CDP_Mtf(values,offsets,aValueCount));
  }
  if (aCodec == 4)
  {
    Jt_U8 aChop;
    if (!theReader.ReadU8 (aChop))
      return EncodedDataHandle();

    if (aChop == 0)
      return loadCDP2 (theReader);

    Jt_I32 aBias;
    Jt_U8  aSpan;
    if (!theReader.ReadI32 (aBias)
     || !theReader.ReadU8  (aSpan))
      return EncodedDataHandle();

    // load chopped MSB and LSB data
    JtDecode_Int32CDP aMSBData;
    JtDecode_Int32CDP aLSBData;
    if (!aMSBData.Load2 (theReader)
     || !aLSBData.Load2 (theReader))
      return EncodedDataHandle();

    if (aMSBData.GetOutValCount() != aValueCount
     || aLSBData.GetOutValCount() != aValueCount)
    {
      ALARM ("Error: Chopped data size mismatch");
      return EncodedDataHandle();
    }

    return EncodedDataHandle (
      new JtDecode_Int32CDP_Chopper (aMSBData, aLSBData, aChop, aSpan, aBias));
  }

  // Null, bitlength or arithmetic codec: read CodeText length
  Jt_I32 aCodeTextLength;
  if (!theReader.ReadI32 (aCodeTextLength) || aCodeTextLength <= 0) {
    throw std::runtime_error("JT CDP code length="+std::to_string(aCodeTextLength)+" codec="+std::to_string(aCodec)+" count="+std::to_string(aValueCount)+" at="+std::to_string(theReader.GetPosition()));
    return EncodedDataHandle();
  }

  // Null codec
  if (aCodec == 0)
  {
    Jt_VecU32 aCodeText;
    if (!theReader.ReadVec (aCodeText, (aCodeTextLength + 31) / 32))
      return EncodedDataHandle();

    return EncodedDataHandle (new JtDecode_Int32CDP_Null (aCodeText.Move()));
  }

  // Bits based codec
  JtData_SingleHandle<JtDecode_Int32CDP_Bits> aData;
  switch (aCodec)
  {
  case 1: // Bitlength codec
    if(theReader.Model()->MajorVersion()>=10) aData=new JtDecode_Int32CDP_Bitlength3(theReader);
    else aData = new JtDecode_Int32CDP_Bitlength2 (theReader);
    break;

  case 3: // Arithmetic codec
    aData = new JtDecode_Int32CDP_Arithmetic (theReader);
    break;

  default: // Unknown codec
    TRACE ("Unknown Codec (" + aCodec + ")!");
    return EncodedDataHandle();
  }

  // Bitlength or arithmetic codec: set expected output values count
  aData->SetOutValCount (aValueCount);

  // Bitlength or arithmetic codec: read CodeText data
  if (!aData->Load ((aCodeTextLength + 31) / 32))
    return EncodedDataHandle();

  // Arithmetic codec: read probability context and load OOB data
  if (aCodec == 3)
  {
    // read probability context
    JtData_Vector<JtDecode_ProbContextI32> aProbContexts (1);
    {
      JtDecode_BitReader::Raw aContextReader (theReader);
      if (!(theReader.Model()->MajorVersion()>=10 ? aProbContexts[0].ReadMk3(aContextReader) : aProbContexts[0].ReadMk2 (aContextReader)))
        return EncodedDataHandle();
    }
    bool hasEscape=false;
    for(size_t i=0;i<aProbContexts[0].Size();++i) hasEscape |= aProbContexts[0][i].symbol==-2;
    aData->SetProbContexts (aProbContexts.Move());

    if(hasEscape || theReader.Model()->MajorVersion()<10) {
      JtDecode_Int32CDP anOOBData;
      if (!anOOBData.Load2 (theReader)) return EncodedDataHandle();
      aData->SetOOBData (anOOBData);
    }
  }

  return EncodedDataHandle (aData);
}
