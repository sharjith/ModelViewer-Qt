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

#include <JtDecode_VertexData_Deering.hxx>

//#define NO_JT_MULTITHREADING
#include <JtData_Parallel.hxx>
#include <cmath>

namespace
{
  static const Standard_Integer TABLE_BITS = 13;

  struct SinCos
  {
    Standard_Real Sin, Cos;
    void Set (Standard_Real aValue)
    {
      Sin = sin (aValue);
      Cos = cos (aValue);
    }
  };

  static Standard_Real Phi[16]= {0.904837, 0.818731, 0.740818,	0.670320,	0.606531,	0.548812, 0.449329, 0.367879,
                                 0.22313,  0.135335, 0.090484,	0.163746,	0.222245,	0.268128,	0.303265,	0.329287};
  class LookupTable
  {
    static const size_t BITS_SIZE = 16;
    static const size_t TABLE_SIZE = 1 << TABLE_BITS;
  public:
    SinCos Theta[TABLE_SIZE + BITS_SIZE];
    SinCos Psi  [TABLE_SIZE + BITS_SIZE];
    LookupTable()
    {
      Standard_Real aMaxPsi     = 0.615479709;
      Standard_Real aTableSizeF = (Standard_Real) TABLE_SIZE;
      size_t i = 0;
      for ( ; i <= TABLE_SIZE; ++i)
      {
        Theta[i].Set (std::asin (std::tan (aMaxPsi * (TABLE_SIZE - i) / aTableSizeF)));
        Psi  [i].Set (aMaxPsi * (i / aTableSizeF));
      }
      size_t j = 0;
      for (i=TABLE_SIZE+1 ; i < TABLE_SIZE + BITS_SIZE; i++,j++)
      {
        Theta[i].Set (std::asin (std::tan (Phi[j])));
        Psi  [i].Set (Phi[j]);
      }
    }
  };

  static const LookupTable LOOKUP_TABLE;
}

JtDecode_VertexData_Deering::JtDecode_VertexData_Deering (const Jt_U8      theNbBits,
                                                          JtDecode_Unpack& theUnpacker, bool packed)
  : JtDecode_VertexData (packed ? 1 : 4, theUnpacker)
  , myNbBits (theNbBits), myPacked(packed) {
    if(!theNbBits || theNbBits>13) throw std::runtime_error("Unsupported JT normal quantization");
  }

Standard_Integer JtDecode_VertexData_Deering::getOutCompCount (Standard_Size)
{
  return 3;
}

void JtDecode_VertexData_Deering::decode (Decoded::Ref theResults)
{
  Jt_VecU32 aSextantCodes, anOctantCodes, aThetaCodes, aPsiCodes;

  JtData_Parallel::TaskGroup aDecodeTasks;
  if(myPacked) {
    Jt_VecI32 codes(decodePackage(0));
    aSextantCodes.Allocate(codes.Count()); anOctantCodes.Allocate(codes.Count());
    aThetaCodes.Allocate(codes.Count()); aPsiCodes.Allocate(codes.Count());
    const uint32_t mask=(1u<<myNbBits)-1;
    for(int i=0;i<codes.Count();++i) {
      const auto c=uint32_t(codes[i]);
      aSextantCodes[i]=(c>>(2*myNbBits+3))&7; anOctantCodes[i]=(c>>(2*myNbBits))&7;
      aThetaCodes[i]=(c>>myNbBits)&mask; aPsiCodes[i]=c&mask;
    }
  } else {
  aDecodeTasks.Run (getDecodingFunctor (0, aSextantCodes));
  aDecodeTasks.Run (getDecodingFunctor (1, anOctantCodes));
  aDecodeTasks.Run (getDecodingFunctor (2, aThetaCodes));
  aDecodeTasks.Run (getDecodingFunctor (3, aPsiCodes));
  aDecodeTasks.Wait();
  }

  Standard_Integer anOffset = TABLE_BITS - myNbBits;
  for (Decoded::SizeType i = 0; i < theResults.Count(); ++i)
  {
    Jt_U32 aSextant = aSextantCodes[i];
    if(aSextant>5 || aThetaCodes[i]+(aSextant&1)>unsigned(1u<<myNbBits) || aPsiCodes[i]>=unsigned(1u<<myNbBits))
      throw std::runtime_error("Invalid JT normal code");

    const SinCos& aThetaPar = LOOKUP_TABLE.Theta[(aThetaCodes[i] + (aSextant & 1)) << anOffset];
    const SinCos& aPsiPar   = LOOKUP_TABLE.Psi  [ aPsiCodes  [i]                   << anOffset];

    Jt_F32 x = static_cast <Jt_F32> (aPsiPar.Cos * aThetaPar.Cos);
    Jt_F32 y = static_cast <Jt_F32> (aPsiPar.Sin);
    Jt_F32 z = static_cast <Jt_F32> (aPsiPar.Cos * aThetaPar.Sin);

    // Change coordinates based on the sextant
    Jt_DirF32 aResult;
    switch (aSextant)
    {
    default:
    case 0: // No op
      aResult.X = x;
      aResult.Y = y;
      aResult.Z = z;
      break;

    case 1: // Mirror about x=z plane
      aResult.X = z;
      aResult.Y = y;
      aResult.Z = x;
      break;

    case 2: // Rotate CW
      aResult.X = y;
      aResult.Y = z;
      aResult.Z = x;
      break;

    case 3: // Mirror about x=y plane
      aResult.X = y;
      aResult.Y = x;
      aResult.Z = z;
      break;

    case 4: // Rotate CCW
      aResult.X = z;
      aResult.Y = x;
      aResult.Z = y;
      break;

    case 5: // Mirror about y=z plane
      aResult.X = x;
      aResult.Y = z;
      aResult.Z = y;
      break;
    }

    // Change some more based on the octant
    Jt_U32 anOctant = anOctantCodes[i];

    // if first bit is 0, negate x component
    if ((anOctant & 4) == 0)
      aResult.X = -aResult.X;

    // if second bit is 0, negate y component
    if ((anOctant & 2) == 0)
      aResult.Y = -aResult.Y;

    // if third bit is 0, negate z component
    if ((anOctant & 1) == 0)
      aResult.Z = -aResult.Z;

    theResults[i] = aResult;
  }
}
