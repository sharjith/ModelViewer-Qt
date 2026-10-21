// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original bounded UV transport reader, 2026-10-21.
// Siemens JT 9.5 section 8.1.6 and JT 10.0 section 13.1.5.
#pragma once
#include <JtDecode_VertexData.hxx>
#include <JtData_Limits.hxx>
#include <cmath>
#include <cstring>

namespace JtDecode_VertexUV {
inline Jt_VecI32::Mover Codes(JtData_Reader& reader,unsigned count,JtDecode_Unpack& predictor) {
    JtDecode_Int32CDP packet;
    if(!packet.Load2(reader)||packet.GetOutValCount()!=int(count))
        throw std::runtime_error("Invalid JT texture-coordinate packet count");
    Jt_VecI32 values(packet.DecodeU32(predictor));
    if(values.Count()!=int(count))throw std::runtime_error("Invalid decoded JT texture-coordinate count");
    return values.Move();
}
inline JtDecode_VertexData::Decoded::Mover Read(JtData_Reader& reader,unsigned components,int expectedCount) {
    if(reader.Model()->MajorVersion()<9)throw std::runtime_error("Unsupported older JT texture-coordinate collection");
    Jt_U32 count;Jt_U8 storedComponents,bits;
    if(!reader.ReadU32(count)||!reader.ReadU8(storedComponents)||!reader.ReadU8(bits))
        throw std::runtime_error("Truncated JT texture-coordinate header");
    if(count>16000000||expectedCount<0||count!=unsigned(expectedCount)||components!=2||storedComponents!=components)
        throw std::runtime_error("Unsupported/invalid JT texture-coordinate count/components");
    if(bits>24)throw std::runtime_error("Invalid JT texture-coordinate quantization width");
    JtData_Limits::allocation(size_t(count)*components*sizeof(float));
    JtDecode_VertexData::Decoded result{int(count),int(components)};
    struct Quantizer {float lo,hi;Jt_U8 width;};
    Quantizer q[2]{};
    if(bits)for(auto& v:q) {
        if(!reader.ReadF32(v.lo)||!reader.ReadF32(v.hi)||!reader.ReadU8(v.width))
            throw std::runtime_error("Truncated JT texture-coordinate quantizer");
        if(v.width>24||!std::isfinite(v.lo)||!std::isfinite(v.hi)||v.lo>v.hi)
            throw std::runtime_error("Invalid JT texture-coordinate quantizer");
        if(!v.width&&v.lo!=v.hi)throw std::runtime_error("Unsupported nonconstant zero-bit JT texture-coordinate quantizer");
    }
    const bool transitional=reader.Model()->MajorVersion()==9;
    for(unsigned c=0;c<components;++c) {
        // Quantized vectors and modern binary IEEE words use Lag1; 9.x
        // exponent/mantissa vectors are raw integer fields.
        Jt_VecI32 values(Codes(reader,count,!bits&&transitional?JtDecode_Unpack_Null:JtDecode_Unpack_Lag1));
        Jt_VecI32 mantissae;
        if(!bits&&transitional)mantissae=Codes(reader,count,JtDecode_Unpack_Null);
        for(unsigned i=0;i<count;++i) {
            if((i&1023)==0)JtData_Limits::poll();
            uint32_t code=uint32_t(values[int(i)]);float value;
            if(bits) {
                const auto& v=q[c];const uint32_t maxCode=v.width?(1u<<v.width)-1:0;
                if(code>maxCode)throw std::runtime_error("JT texture-coordinate code exceeds quantizer width");
                value=v.width?float(double(v.lo)+double(code)*(double(v.hi)-v.lo)/maxCode):v.lo;
            } else {
                if(transitional) {
                    const uint32_t mantissa=uint32_t(mantissae[int(i)]);
                    if(code>0x1ff||mantissa>0x7fffff)throw std::runtime_error("Invalid JT texture-coordinate exponent/mantissa");
                    code=(code<<23)|mantissa;
                }
                std::memcpy(&value,&code,sizeof(value));
            }
            // UVs may tile or be negative: do not clamp to the unit square.
            if(!std::isfinite(value))throw std::runtime_error("Nonfinite JT texture coordinate");
            result[int(i)][int(c)]=value;
        }
    }
    Jt_U32 hash;if(!reader.ReadU32(hash))throw std::runtime_error("Truncated JT texture-coordinate hash");
    return result.Move();
}
}
