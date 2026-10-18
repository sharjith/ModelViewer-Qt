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

#include <JtDecode_Unpack.hxx>

// Internal data types
typedef JtData_VectorRef<int32_t, int32_t> Values; // reference to array of values
typedef Values::SizeType I; // index type
typedef Values::ValType  T; // data type

// Unpacker template
template <T Predict (I i, T v1, T v2, T v4),
          T Decode  (T predicted, T residual)>
class Unpack
{
public:
  Unpack (Values theValues)
  {
    for (I i = 4; i < theValues.Count(); i++)
    {
      T v1 = theValues[i - 1];
      T v2 = theValues[i - 2];
      T v4 = theValues[i - 4];
      T aResidual  = theValues[i];
      T aPredicted = Predict (i, v1, v2, v4);
      T aResult    = Decode (aPredicted, aResidual);
      theValues[i] = aResult;
    }
  }
};

// Predictors
T PredLag1     (I  , T v1, T   , T   ) { return v1; }
T PredLag2     (I  , T   , T v2, T   ) { return v2; }
T PredRamp     (I i, T   , T   , T   ) { return i; }
T PredStride1  (I  , T v1, T v2, T   ) { return v1 + (v1 - v2); }
T PredStride2  (I  , T   , T v2, T v4) { return v2 + (v2 - v4); }
T PredStripIdx (I  , T   , T v2, T v4) { T d = v2 - v4;
                                         return v2 + ((-8 < d && d < 8) ? d : 2); }
// Decoders
T Add (T predicted, T residual) { return predicted + residual; }
T Xor (T predicted, T residual) { return predicted ^ residual; }

// Unpackers
void JtDecode_Unpack_Null    (Values) {}
void JtDecode_Unpack_Lag1    (Values theValues) { (Unpack<PredLag1    , Add>) (theValues); }
void JtDecode_Unpack_Lag2    (Values theValues) { (Unpack<PredLag2    , Add>) (theValues); }
void JtDecode_Unpack_Xor1    (Values theValues) { (Unpack<PredLag1    , Xor>) (theValues); }
void JtDecode_Unpack_Xor2    (Values theValues) { (Unpack<PredLag2    , Xor>) (theValues); }
void JtDecode_Unpack_Ramp    (Values theValues) { (Unpack<PredRamp    , Add>) (theValues); }
void JtDecode_Unpack_Stride1 (Values theValues) { (Unpack<PredStride1 , Add>) (theValues); }
void JtDecode_Unpack_Stride2 (Values theValues) { (Unpack<PredStride2 , Add>) (theValues); }
void JtDecode_Unpack_StripIdx(Values theValues) { (Unpack<PredStripIdx, Add>) (theValues); }
