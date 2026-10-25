// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original bounded UV transport reader, 2026-10-21.
// Scalar texture-coordinate support, 2026-10-21.
// Siemens JT 9.5 section 8.1.6 and JT 10.0 section 13.1.5.
#pragma once
#include <JtDecode_VertexData.hxx>
#include <JtData_Limits.hxx>
#include <cmath>
#include <cstring>

namespace JtDecode_VertexUV {
// Original compatibility adapter, 2026-10-25. The retained JT10 reference
// describes Bob Jenkins' lookup2 word hash (Annex C). Authentic modern exports
// also use this hash over whole component arrays, with no IEEE-word predictor.
inline uint32_t HashWords(const Jt_VecI32& words,uint32_t seed) {
    uint32_t a=0x9e3779b9u,b=a,c=seed;
    const auto mix=[&] {
        constexpr unsigned rounds[][3]={{13,8,13},{12,16,5},{3,10,15}};
        for(const auto& shifts:rounds) {
            a=(a-b-c)^(c>>shifts[0]);
            b=(b-c-a)^(a<<shifts[1]);
            c=(c-a-b)^(b>>shifts[2]);
        }
    };
    int i=0;
    while(words.Count()-i>=3) {
        if((i&1023)==0)JtData_Limits::poll();
        a+=uint32_t(words[i]);b+=uint32_t(words[i+1]);c+=uint32_t(words[i+2]);
        mix();i+=3;
    }
    c+=uint32_t(words.Count());
    if(i<words.Count())a+=uint32_t(words[i]);
    if(i+1<words.Count())b+=uint32_t(words[i+1]);
    mix();return c;
}
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
    if(count>16000000||expectedCount<0||count!=unsigned(expectedCount)||(components!=1&&components!=2)||storedComponents!=components)
        throw std::runtime_error("Unsupported/invalid JT texture-coordinate count/components");
    if(bits>24)throw std::runtime_error("Invalid JT texture-coordinate quantization width");
    JtData_Limits::allocation(size_t(count)*components*sizeof(float));
    JtDecode_VertexData::Decoded result{int(count),int(components)};
    struct Quantizer {float lo,hi;Jt_U8 width;};
    Quantizer q[2]{};
    if(bits)for(unsigned c=0;c<components;++c) {
        auto& v=q[c];
        if(!reader.ReadF32(v.lo)||!reader.ReadF32(v.hi)||!reader.ReadU8(v.width))
            throw std::runtime_error("Truncated JT texture-coordinate quantizer");
        if(v.width>24||!std::isfinite(v.lo)||!std::isfinite(v.hi)||v.lo>v.hi)
            throw std::runtime_error("Invalid JT texture-coordinate quantizer");
        if(!v.width&&v.lo!=v.hi)throw std::runtime_error("Unsupported nonconstant zero-bit JT texture-coordinate quantizer");
    }
    const bool transitional=reader.Model()->MajorVersion()==9;
    Jt_VecI32 binaryWords[2];
    if(!bits&&!transitional) {
        for(unsigned c=0;c<components;++c)
            binaryWords[c]=Codes(reader,count,JtDecode_Unpack_Null);
        Jt_U32 hash;if(!reader.ReadU32(hash))throw std::runtime_error("Truncated JT texture-coordinate hash");
        uint32_t rawHash=0;
        for(unsigned c=0;c<components;++c)rawHash=HashWords(binaryWords[c],rawHash);
        // Select the authentic unpredicted form by the complete stored hash,
        // never by whether a trial interpretation happens to look finite.
        if(rawHash!=hash)
            for(unsigned c=0;c<components;++c)JtDecode_Unpack_Lag1(binaryWords[c]);
        for(unsigned c=0;c<components;++c)for(unsigned i=0;i<count;++i) {
            if((i&1023)==0)JtData_Limits::poll();
            const uint32_t word=uint32_t(binaryWords[c][int(i)]);float value;
            std::memcpy(&value,&word,sizeof(value));
            if(!std::isfinite(value))throw std::runtime_error("Nonfinite JT texture coordinate");
            result[int(i)][int(c)]=value;
        }
        return result.Move();
    }
    for(unsigned c=0;c<components;++c) {
        // Quantized vectors use Lag1; 9.x exponent/mantissa vectors are raw
        // integer fields. Modern binary IEEE words were handled above.
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
