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

#include <JtDecode_BitReader.hxx>

//=======================================================================
//function : Base Constructor
//purpose  : Initialize the reader
//=======================================================================
JtDecode_BitReader::Base::Base (JtData_Reader& theReader, Standard_Boolean theNeedSwap)
: myReader     (&theReader)
, myNeedSwap   (theNeedSwap)
, myBitsBuf    (0)
, myBitsLoaded (0)
, myByteBuf    (0L)
, myNextDWord  (0L) {}

//=======================================================================
//function : Base Destructor
//purpose  : Cleanup
//=======================================================================
JtDecode_BitReader::Base::~Base()
{
  myReader->Unload (myByteBuf);
}

//=======================================================================
//function : Raw Constructor
//purpose  : Specify the byte swapping mode
//=======================================================================
JtDecode_BitReader::Raw::Raw (JtData_Reader& theReader)
  : Base (theReader, JtData_Model::IsLittleEndianHost) {}

//=======================================================================
//function : Raw::Load
//purpose  : Load the given number of bits from the external reader to
//           internal buffers that supply data for reading operations.
//           A subsequent call discards all previously loaded bits and
//           loads new bits starting right after the last bit loaded by
//           the previous call.
//=======================================================================
Standard_Boolean JtDecode_BitReader::Raw::Load (const unsigned theBitsCount)
{
  if(theBitsCount<=myBitsLoaded) return Standard_True;
  // Calculate number of bytes that should be read to new byte buffer
  // to provide the requested number of bits
  Standard_Size aNewBytesCount = (theBitsCount - myBitsLoaded + 7) / 8;
  if (aNewBytesCount > 0)
  {
    // Calculate size of data to preload from the byte buffer in order to
    // leave an integer number of double words in it
    Standard_Size aPreloadBytesCount = aNewBytesCount % sizeof (uint32_t);
    Standard_Size aPreloadBitsCount  = aPreloadBytesCount * 8;

    // Check if there is enough space in the bit buffer to store the preloaded bits
    if (myBitsLoaded + aPreloadBitsCount > 32)
      return Standard_False;

    // Read the needed number of bytes to the byte buffer
    if (!myReader)
      return Standard_False;
    myReader->Unload (myByteBuf);
    myByteBuf = myReader->Load (aNewBytesCount);
    if (!myByteBuf)
      return Standard_False;

    // Load the first incomplete double word from the byte buffer to the bit buffer
    uint32_t aFirstBytes = 0;
    memcpy (&aFirstBytes, myByteBuf, aPreloadBytesCount);
    if (JtData_Model::IsLittleEndianHost)
      ByteSwap (aFirstBytes);

    if (myBitsLoaded)
    {
      myBitsBuf    |= aFirstBytes >> myBitsLoaded;
      myBitsLoaded += aPreloadBitsCount;
    }
    else
    {
      myBitsBuf    = aFirstBytes;
      myBitsLoaded = aPreloadBitsCount;
    }

    // Set next double word pointer to the first complete double word in the byte buffer
    myNextDWord = reinterpret_cast <const uint32_t*> (
                  reinterpret_cast <const uint8_t*> (myByteBuf) + aPreloadBytesCount);
    myWordEnd=myNextDWord+(aNewBytesCount-aPreloadBytesCount)/4;
  }

  // Done
  return Standard_True;
}

//=======================================================================
//function : U32 Constructor
//purpose  : Specify the byte swapping mode
//=======================================================================
JtDecode_BitReader::U32::U32 (JtData_Reader& theReader)
  : Base (theReader, theReader.Model()->IsFileLE() != JtData_Model::IsLittleEndianHost) {}

//=======================================================================
//function : U32::Load
//purpose  : Read an array of U32 values with the given size from the
//           external reader to internal buffer that supplies data for
//           reading operations.
//           A subsequent call discards all previously loaded bits and
//           reads a new array.
//=======================================================================
Standard_Boolean JtDecode_BitReader::U32::Load (const Standard_Size theValuesCount)
{
  // Free the byte buffer if any
  myReader->Unload (myByteBuf);

  // Read the values and initialize properties
  myBitsBuf    = 0;
  myBitsLoaded = 0;
  myByteBuf    = myReader->Load (theValuesCount * sizeof (Jt_U32));
  myNextDWord  = reinterpret_cast <const uint32_t*> (myByteBuf);
  myWordEnd=myNextDWord ? myNextDWord+theValuesCount : nullptr;

  // Done
  return myByteBuf != 0L;
}

//=======================================================================
//function : U32::LoadVec
//purpose  : Read a VecU32 from the external reader to internal buffer
//           that supplies data for reading operations.
//           A subsequent call discards all previously loaded bits and
//           reads a new VecU32.
//=======================================================================
Standard_Boolean JtDecode_BitReader::U32::LoadVec()
{
  // Read the U32 values count and load array with corresponding size
  Jt_I32 aValuesCount;
  return myReader->ReadI32 (aValuesCount) && Load (aValuesCount);
}
