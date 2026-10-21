// Original ModelViewer-Qt addition, 2026-10-19.
// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <JtDecode_VertexData.hxx>
#include <JtData_Limits.hxx>
#include <cmath>
#include <cstring>
#include <algorithm>

// JT 10.0 sections 13.1.6, 13.1.11 and 13.2.1: lossless IEEE-754
// component arrays and modern packed RGB/HSV quantizers.
// Modern quantized colour transport added: 2026-10-21.
// Transitional component-array colour transport added: 2026-10-21.
namespace JtDecode_VertexColors {
struct Range {float lo=0,hi=0;unsigned width=0;};
inline Jt_VecI32::Mover Codes(JtData_Reader& reader,unsigned count,JtDecode_Unpack& predictor) {
    JtDecode_Int32CDP packet;
    if(!packet.Load2(reader)||packet.GetOutValCount()!=int(count))throw std::runtime_error("Invalid JT colour component packet");
    Jt_VecI32 values(packet.DecodeU32(predictor));
    if(values.Count()!=int(count))throw std::runtime_error("Invalid decoded JT colour count");
    return values.Move();
}
inline float Dequantize(const Range& r,uint32_t code) {
    const uint32_t maxCode=r.width?(1u<<r.width)-1:0;
    if(code>maxCode)throw std::runtime_error("JT colour component code exceeds quantizer width");
    return r.width?float(double(r.lo)+double(code)*(double(r.hi)-r.lo)/maxCode):r.lo;
}
inline void ConvertHSV(JtDecode_VertexData::Decoded& result,unsigned count) {
    for(unsigned i=0;i<count;++i) {
        if((i&1023)==0)JtData_Limits::poll();
        const float h=result[int(i)][0],sat=result[int(i)][1],v=result[int(i)][2];
        const unsigned sector=unsigned(h==6.f?0.f:std::floor(h));
        const float f=h==6.f?0.f:h-std::floor(h),p=v*(1-sat),t=v*(1-sat*(1-f)),u=v*(1-sat*f);
        switch(sector){
        case 0:result[int(i)][0]=v;result[int(i)][1]=t;result[int(i)][2]=p;break;
        case 1:result[int(i)][0]=u;result[int(i)][1]=v;result[int(i)][2]=p;break;
        case 2:result[int(i)][0]=p;result[int(i)][1]=v;result[int(i)][2]=t;break;
        case 3:result[int(i)][0]=p;result[int(i)][1]=u;result[int(i)][2]=v;break;
        case 4:result[int(i)][0]=t;result[int(i)][1]=p;result[int(i)][2]=v;break;
        case 5:result[int(i)][0]=v;result[int(i)][1]=p;result[int(i)][2]=u;break;
        default:throw std::runtime_error("Invalid JT hue sector");
        }
    }
}
inline JtDecode_VertexData::Decoded::Mover Read(JtData_Reader& reader,unsigned components,int expectedCount) {
    if(reader.Model()->MajorVersion()<9)throw std::runtime_error("Unsupported older JT colour collection");
    const bool transitional=reader.Model()->MajorVersion()==9;
    Jt_U32 count;Jt_U8 storedComponents,bits;
    if(!reader.ReadU32(count)||!reader.ReadU8(storedComponents)||!reader.ReadU8(bits))throw std::runtime_error("Truncated JT vertex colour header");
    if(count>16000000||expectedCount<0||count!=unsigned(expectedCount)||(components!=3&&components!=4)||storedComponents!=components)
        throw std::runtime_error("Invalid JT vertex colour count/components");
    if(bits>8)throw std::runtime_error("Invalid JT vertex colour quantization width");
    JtData_Limits::allocation(size_t(count)*components*sizeof(float));
    JtDecode_VertexData::Decoded result{int(count),int(components)};
    if(bits) {
        Range q[4];Jt_U8 space;
        if(!reader.ReadU8(space)||space>1)throw std::runtime_error("Invalid JT colour space");
        for(unsigned c=0;c<4;++c) {
            Jt_U8 width;
            if(space==0) {
                if(!reader.ReadF32(q[c].lo)||!reader.ReadF32(q[c].hi)||!reader.ReadU8(width))throw std::runtime_error("Truncated JT colour quantizer");
            } else {
                q[c].hi=c==0?6.f:1.f;
                if(!reader.ReadU8(width))throw std::runtime_error("Truncated JT HSV quantizer");
            }
            q[c].width=width;
            if(width>8||!std::isfinite(q[c].lo)||!std::isfinite(q[c].hi)||q[c].lo<0||q[c].lo>q[c].hi||q[c].hi>(space==1&&c==0?6.f:1.f))
                throw std::runtime_error("Invalid JT colour quantizer range/width");
            if(c<components&&!width&&q[c].lo!=q[c].hi)throw std::runtime_error("Unsupported nonconstant zero-bit JT colour quantizer");
        }
        if(transitional) {
            // JT 9.5 Figure 230 has four Lag1 component vectors, including
            // Alpha Codes even when the colour binding exposes RGB only.
            for(unsigned c=0;c<4;++c) {
                Jt_VecI32 values(Codes(reader,count,JtDecode_Unpack_Lag1));
                for(unsigned i=0;i<count;++i) {
                    if((i&1023)==0)JtData_Limits::poll();
                    const auto value=Dequantize(q[c],uint32_t(values[int(i)]));
                    if(c<components)result[int(i)][int(c)]=value;
                }
            }
        } else {
            unsigned totalBits=0;for(unsigned c=0;c<components;++c)totalBits+=q[c].width;
            Jt_VecI32 codes(Codes(reader,count,JtDecode_Unpack_Null));
            for(unsigned i=0;i<count;++i) {
                if((i&1023)==0)JtData_Limits::poll();
                uint32_t packed=uint32_t(codes[int(i)]);
                if(totalBits<32&&(packed>>totalBits)!=0)throw std::runtime_error("JT packed colour code exceeds quantizer widths");
                for(unsigned c=components;c>0;) {
                    --c;const auto& r=q[c];const uint32_t maxCode=r.width?(1u<<r.width)-1:0;
                    result[int(i)][int(c)]=Dequantize(r,packed&maxCode);packed>>=r.width;
                }
            }
        }
        if(space==1)ConvertHSV(result,count);
    } else {
        for(unsigned c=0;c<components;++c) {
            Jt_VecI32 values(Codes(reader,count,transitional?JtDecode_Unpack_Null:JtDecode_Unpack_Lag1));
            Jt_VecI32 mantissae;
            if(transitional)mantissae=Codes(reader,count,JtDecode_Unpack_Null);
            for(unsigned i=0;i<count;++i) {
                if((i&1023)==0)JtData_Limits::poll();
                uint32_t word=uint32_t(values[int(i)]);
                if(transitional) {
                    const auto mantissa=uint32_t(mantissae[int(i)]);
                    if(word>0x1ff||mantissa>0x7fffff)throw std::runtime_error("Invalid JT colour exponent/mantissa fields");
                    word=(word<<23)|mantissa;
                }
                float value;std::memcpy(&value,&word,sizeof(value));
                if(!std::isfinite(value)||value<0||value>1)throw std::runtime_error("JT vertex colour component is outside [0,1]");
                result[int(i)][int(c)]=value;
            }
        }
    }
    Jt_U32 hash;if(!reader.ReadU32(hash))throw std::runtime_error("Truncated JT vertex colour hash");
    return result.Move();
}
}
