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

#ifndef _JtDecode_Int32CDP_Bits_HeaderFile
#define _JtDecode_Int32CDP_Bits_HeaderFile

#include <JtDecode_Int32CDP.hxx>
#include <JtDecode_BitReader.hxx>
#include <JtDecode_ProbContext.hxx>

//! Base class for bit-based Jt decoding algorithms.
//! Provides the basic decoding interface.
class JtDecode_Int32CDP_Bits : public JtDecode_Int32CDP::EncodedData
                             , public JtDecode_BitReader::U32
{
public:
  //! Initialize.
  JtDecode_Int32CDP_Bits (JtData_Reader& theReader) : JtDecode_BitReader::U32 (theReader) {}

  //! Set count of output values.
  void SetOutValCount (int32_t theOutValCount)
    { myOutValCount = theOutValCount; }

  //! Set probability contexts.
  void SetProbContexts (JtData_Vector<JtDecode_ProbContextI32>::Mover theProbContexts)
    { myProbContexts = theProbContexts; }

  //! Set out-of-band data.
  void SetOOBData (JtDecode_Int32CDP theOOBData)
    { myOOBData = theOOBData; }

  //! Get expected count of output values.
  Standard_EXPORT virtual int32_t GetOutValCount() const;

  //! Allocate the vector and decode the loaded bits.
  Standard_EXPORT virtual Decoded::Mover Decode();

protected:
  //! Decode the loaded bits into an allocated memory.
  Standard_EXPORT virtual void decode (int32_t* theResultPtr, int32_t* theResultEnd);

  //! Decode the loaded bits into an allocated memory using decoded out-of-band data.
  Standard_EXPORT virtual void decode (int32_t* theResultPtr,
                                       int32_t* theResultEnd,
                                 const int32_t* theOOBDataPtr);

protected:
  int32_t                                myOutValCount;
  JtData_Vector<JtDecode_ProbContextI32> myProbContexts;
  JtDecode_Int32CDP                      myOOBData;
  const int32_t* myOOBEnd=nullptr;
};

#endif
