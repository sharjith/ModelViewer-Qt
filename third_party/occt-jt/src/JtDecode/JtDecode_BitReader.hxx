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

#ifndef _JtDecode_BitReader_HeaderFile
#define _JtDecode_BitReader_HeaderFile

#include <JtData_Reader.hxx>
#include <JtData_ByteSwap.hxx>

//! Bit Reader - implements buffered reading of subsequent bit fields from a JT file.
//!
//! Usage:
//! 1. Create an object of either Raw or U32 member class depending of needed loading behavior:
//!    - Raw for loading raw bytes from the external reader,
//!    - U32 for loading U32 values the external reader;
//! 2. Load total number of bits from an external reader
//!    using Raw::Load, U32::LoadVec or U32::LoadArray method;
//! 3. Use Read... methods to subsequently read all the loaded bits.
//! 4. Load method may be called again to load some more bits.
class JtDecode_BitReader
{
public:

  //! Base Data storage and reading implementation.
  class Base
  {
  public:
    //! Read one bit from the buffer.
    inline uint32_t ReadBit()
    {
      if (myBitsLoaded == 0)
      {
        loadBits();
        myBitsLoaded = 31;
      }
      else
        myBitsLoaded--;

      uint32_t aResult = myBitsBuf >> 31;
      myBitsBuf <<= 1;
      return aResult;
    }

    //! Read the given number of bits (1...32) from the buffer as an unsigned integer.
    //! @param theBitsCount - parameter may not be 0.
    inline uint32_t ReadU32 (const unsigned theBitsCount)
    {
      if(!theBitsCount || theBitsCount>32) throw std::runtime_error("Invalid JT bit width");
      return bitBuffer (theBitsCount) >> (32 - theBitsCount);
    }

    //! Read the given number of bits (1...32) from the buffer as a signed integer.
    //! @param theBitsCount - parameter may not be 0.
    inline int32_t  ReadI32 (const unsigned theBitsCount)
    {
      if(!theBitsCount || theBitsCount>32) throw std::runtime_error("Invalid JT signed bit width");
      return static_cast <int32_t> (bitBuffer (theBitsCount)) >> (32 - theBitsCount);
    }

    //! Read the given number of bits (0...32) from the buffer as an unsigned integer.
    //! Returns 0 is the parameter is 0.
    inline uint32_t ReadU32Or0 (const unsigned theBitsCount)
    {
      return theBitsCount ? ReadU32 (theBitsCount) : 0;
    }

    //! Read the given number of bits (0...32) from the buffer as a signed integer.
    //! Returns 0 is the parameter is 0.
    inline int32_t  ReadI32Or0 (const unsigned theBitsCount)
    {
      return theBitsCount ? ReadI32 (theBitsCount) : 0;
    }

    //! Destructor.
    ~Base();

  protected:
    //! Protected constructor.
    Base (JtData_Reader& theReader, Standard_Boolean theNeedSwap);

    //! Load next 32 bits from the byte buffer to the bit buffer.
    inline void loadBits()
    {
      if(!myNextDWord || myNextDWord>=myWordEnd) throw std::runtime_error("Truncated JT bitstream");
      if (myNeedSwap)
        ByteSwap (*myNextDWord++, myBitsBuf);
      else
        myBitsBuf = *myNextDWord++;
    }

    //! Obtain a bit buffer containing the needed number of bits.
    inline uint32_t bitBuffer (const unsigned theBitsCount)
    {
      uint32_t aResult;
      if (myBitsLoaded >= theBitsCount)
      {
        aResult = myBitsBuf;
        myBitsBuf = theBitsCount==32 ? 0 : myBitsBuf<<theBitsCount;
        myBitsLoaded -= theBitsCount;
      }
      else if (myBitsLoaded == 0)
      {
        loadBits();
        aResult = myBitsBuf;
        myBitsBuf = theBitsCount==32 ? 0 : myBitsBuf<<theBitsCount;
        myBitsLoaded = 32 - theBitsCount;
      }
      else
      {
        aResult = myBitsBuf;
        Standard_Size aHeadSize = myBitsLoaded;
        Standard_Size aTailSize = theBitsCount - aHeadSize;
        loadBits();
        aResult |= myBitsBuf >> aHeadSize;
        myBitsBuf = aTailSize==32 ? 0 : myBitsBuf<<aTailSize;
        myBitsLoaded = 32 - aTailSize;
      }
      return aResult;
    }

  protected:
    JtData_Reader*   myReader;     //!< external reader
    Standard_Boolean myNeedSwap;   //!< is byte swapping needed after reading from the external reader

    uint32_t         myBitsBuf;    //!< bit buffer (32 bits) - acts as a queue: bits are left-shifted out when read
    Standard_Size    myBitsLoaded; //!< number of bits rest in the bit buffer

    const void*      myByteBuf;    //!< byte buffer (allocated by the external reader)
    const uint32_t*  myNextDWord;  //!< address of the next double word to be read from the byte buffer
    const uint32_t* myWordEnd=nullptr;
  };

  //! Implementation of raw bytes loading behavior.
  class Raw : public Base
  {
  public:
    //! Constructor
    Raw (JtData_Reader& theReader);

    //! Load the given number of bits from the external reader to
    //! internal buffers that supply data for reading operations.
    //! A subsequent call discards all previously loaded bits and
    //! loads new bits starting right after the last bit loaded by
    //! the previous call.
    Standard_Boolean Load (const unsigned theBitsCount);
  };

  //! Implementation of U32 values loading behavior.
  class U32 : public Base
  {
  public:
    //! Constructor.
    U32 (JtData_Reader& theReader);

    //! Read an array of U32 values with the given size from the
    //! external reader to internal buffer that supplies data for
    //! reading operations.
    //! A subsequent call discards all previously loaded bits and
    //! reads a new array.
    Standard_Boolean Load (const Standard_Size theValuesCount);

    //! Read a VecU32 from the external reader to internal buffer
    //! that supplies data for reading operations.
    //! A subsequent call discards all previously loaded bits and
    //! reads a new VecU32.
    Standard_Boolean LoadVec();
  };
};

#endif
