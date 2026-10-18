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

#ifndef _JtData_ByteSwap_HeaderFile
#define _JtData_ByteSwap_HeaderFile

#ifdef __GNUC__
#include <byteswap.h>
#endif

#ifdef __GNUC__

inline void ByteSwap (uint16_t theSrc, uint16_t& theDst) { theDst = (theSrc >> 8) | (theSrc << 8); }

#ifdef bswap_32
inline void ByteSwap (uint32_t theSrc, uint32_t& theDst) { theDst = bswap_32 (theSrc); }
inline void ByteSwap (uint64_t theSrc, uint64_t& theDst) { theDst = bswap_64 (theSrc); }
#elif __GNUC_MINOR__ < 3
inline void ByteSwap (uint32_t theSrc, uint32_t& theDst) { theDst = __bswap_32 (theSrc); }
inline void ByteSwap (uint64_t theSrc, uint64_t& theDst) { theDst = __bswap_64 (theSrc); }
#else
inline void ByteSwap (uint32_t theSrc, uint32_t& theDst) { theDst = __builtin_bswap32 (theSrc); }
inline void ByteSwap (uint64_t theSrc, uint64_t& theDst) { theDst = __builtin_bswap64 (theSrc); }
#endif

#else

inline void ByteSwap (uint16_t theSrc, uint16_t& theDst) { theDst = _byteswap_ushort (theSrc); }
inline void ByteSwap (uint32_t theSrc, uint32_t& theDst) { theDst = _byteswap_ulong  (theSrc); }
inline void ByteSwap (uint64_t theSrc, uint64_t& theDst) { theDst = _byteswap_uint64 (theSrc); }

#endif

template <typename Type, typename TypeRaw>
inline void ByteSwap (Type theSrc, Type& theDst)
{
  // Use unions instead of dereferencing type-punned pointers
  // for example: "reinterpret_cast<uint16_t&> (theSrc)"
  // to be consistent with strict-aliasing rule.
  union { Type Typed; TypeRaw Raw; } aBytesIn = { theSrc };
  union { Type Typed; TypeRaw Raw; } aBytesOut;
  ByteSwap (aBytesIn.Raw, aBytesOut.Raw);
  theDst = aBytesOut.Typed;
}

inline void ByteSwap (int16_t theSrc, int16_t& theDst)      { ByteSwap <int16_t, uint16_t> (theSrc, theDst); }
inline void ByteSwap (int32_t theSrc, int32_t& theDst)      { ByteSwap <int32_t, uint32_t> (theSrc, theDst); }
inline void ByteSwap (int64_t theSrc, int64_t& theDst)      { ByteSwap <int64_t, uint64_t> (theSrc, theDst); }
inline void ByteSwap (const float& theSrc, float& theDst)   { ByteSwap <float,   uint32_t> (theSrc, theDst); }
inline void ByteSwap (const double& theSrc, double& theDst) { ByteSwap <double,  uint64_t> (theSrc, theDst); }

template <typename Type> inline void ByteSwap (Type& theValue) { ByteSwap (theValue, theValue); }

#endif // _JtData_ByteSwap_HeaderFile
