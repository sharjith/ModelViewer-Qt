// Original ModelViewer-Qt addition, 2026-10-19.
// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <JtDecode_VertexData.hxx>
#include <JtData_Limits.hxx>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <string>

// JT 10.0 sections 13.1.6, 13.1.11 and 13.2.1: lossless IEEE-754
// component arrays and modern packed RGB/HSV quantizers.
// Modern quantized colour transport added: 2026-10-21.
// Transitional component-array colour transport added: 2026-10-21.
namespace JtDecode_VertexColors {
struct Range {float lo=0,hi=0;unsigned width=0;};
// JT 10 Annex C lookup2 word hash. Authentic modern exporters use the
// complete chained component-array hash to distinguish raw IEEE words from
// the previously qualified Lag1 transport, as already observed for UV arrays.
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
// JT 9.x section 9.5 delegates to Jenkins hash2(). Its length contribution
// is the byte length even though the input count is expressed in 32-bit words.
inline uint32_t LegacyHashWords(const Jt_VecI32& words,uint32_t seed) {
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
    c+=uint32_t(words.Count())<<2;
    if(i<words.Count())a+=uint32_t(words[i]);
    if(i+1<words.Count())b+=uint32_t(words[i+1]);
    mix();return c;
}
inline Jt_VecI32::Mover Codes(JtData_Reader& reader,unsigned count,JtDecode_Unpack& predictor,
                              const char* field=nullptr,unsigned component=0,
                              const Jt_I32* prefetchedCount=nullptr) {
    JtDecode_Int32CDP packet;
    const auto packetOffset=reader.GetPosition();
    const std::string context=field?std::string(" for ")+field+" component "+std::to_string(component):std::string();
    const bool loaded=prefetchedCount?packet.Load2AfterCount(reader,*prefetchedCount):packet.Load2(reader);
    if(!loaded)throw std::runtime_error("Invalid JT colour component packet header"+context
        +" at offset "+std::to_string(packetOffset));
    if(packet.GetOutValCount()!=int(count))
        throw std::runtime_error("JT colour component packet count mismatch"+context+": expected "
            +std::to_string(count)+", got "+std::to_string(packet.GetOutValCount()));
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
//! `losslessLag1` selects the predictor of JT 10 lossless component arrays. NX-written polyline and point set LODs (C3 samples)
//! store the raw component words (Null predictor); the Lag1 form is retained for the existing tessellation path.
inline JtDecode_VertexData::Decoded::Mover Read(JtData_Reader& reader,unsigned components,int expectedCount,bool losslessLag1=true) {
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
    } else if(!transitional) {
        Jt_VecI32 binaryWords[4];
        for(unsigned c=0;c<components;++c)
            binaryWords[c]=Codes(reader,count,JtDecode_Unpack_Null);
        Jt_U32 hash;if(!reader.ReadU32(hash))throw std::runtime_error("Truncated JT vertex colour hash");
        uint32_t rawHash=0;
        for(unsigned c=0;c<components;++c)rawHash=HashWords(binaryWords[c],rawHash);
        // A matching complete source hash selects raw IEEE words. Otherwise
        // retain the established Lag1 path. The explicit false option used by
        // qualified polyline/point records continues to force raw words.
        if(losslessLag1&&rawHash!=hash)
            for(unsigned c=0;c<components;++c)JtDecode_Unpack_Lag1(binaryWords[c]);
        for(unsigned c=0;c<components;++c)for(unsigned i=0;i<count;++i) {
            if((i&1023)==0)JtData_Limits::poll();
            const uint32_t word=uint32_t(binaryWords[c][int(i)]);float value;
            std::memcpy(&value,&word,sizeof(value));
            if(!std::isfinite(value)||value<0||value>1)throw std::runtime_error("JT vertex colour component is outside [0,1]");
            result[int(i)][int(c)]=value;
        }
        return result.Move();
    } else {
        JtData_Limits::allocation(size_t(count)*components*sizeof(float));
        JtDecode_VertexData::Decoded lagResult{int(count),int(components)};
        uint32_t rawHash=0,lagHash=0;bool rawValid=true,rawOverlapValid=true,lagValid=true;
        bool profileKnown=false,perComponentSeparators=false,hasPrefetchedCount=false;
        Jt_I32 prefetchedCount=0;
        for(unsigned c=0;c<components;++c) {
            const Jt_I32* prefetched=hasPrefetchedCount?&prefetchedCount:nullptr;
            Jt_VecI32 exponents(Codes(reader,count,JtDecode_Unpack_Null,"exponent",c,prefetched));
            hasPrefetchedCount=false;
            Jt_VecI32 mantissae(Codes(reader,count,JtDecode_Unpack_Null,"mantissa",c));
            Jt_VecI32 rawWords{int(count)};
            bool rawComponentValid=true,rawOverlapComponentValid=true,lagComponentValid=true;
            for(unsigned i=0;i<count;++i) {
                if((i&1023)==0)JtData_Limits::poll();
                uint32_t word=uint32_t(exponents[int(i)]);
                const auto mantissa=uint32_t(mantissae[int(i)]);
                if(word>0x1ff){rawComponentValid=false;rawOverlapComponentValid=false;}
                const uint32_t exponentWord=word<<23;
                if(mantissa>0x7fffff)rawComponentValid=false;
                if((mantissa&~0x7fffffu&~exponentWord)!=0)rawOverlapComponentValid=false;
                word=exponentWord|mantissa;rawWords[int(i)]=int32_t(word);
                float value;std::memcpy(&value,&word,sizeof(value));
                if(!std::isfinite(value)||value<0||value>1){rawComponentValid=false;rawOverlapComponentValid=false;}
                result[int(i)][int(c)]=value;
            }
            rawValid=rawValid&&rawComponentValid;
            rawOverlapValid=rawOverlapValid&&rawOverlapComponentValid;
            rawHash=LegacyHashWords(rawWords,rawHash);
            JtDecode_Unpack_Lag1(exponents);JtDecode_Unpack_Lag1(mantissae);
            Jt_VecI32 lagWords{int(count)};
            for(unsigned i=0;i<count;++i) {
                if((i&1023)==0)JtData_Limits::poll();
                uint32_t word=uint32_t(exponents[int(i)]);
                const auto mantissa=uint32_t(mantissae[int(i)]);
                if(word>0x1ff||mantissa>0x7fffff)lagComponentValid=false;
                word=(word<<23)|mantissa;lagWords[int(i)]=int32_t(word);
                float value;std::memcpy(&value,&word,sizeof(value));
                if(!std::isfinite(value)||value<0||value>1)lagComponentValid=false;
                lagResult[int(i)][int(c)]=value;
            }
            lagValid=lagValid&&lagComponentValid;
            lagHash=LegacyHashWords(lagWords,lagHash);
            if(!profileKnown) {
                Jt_U32 marker;if(!reader.ReadU32(marker))throw std::runtime_error("Truncated JT vertex colour hash/profile marker");
                if(c+1<components&&marker==count) {
                    profileKnown=true;prefetchedCount=Jt_I32(marker);hasPrefetchedCount=true;
                } else {
                    if(c+1>=components||!reader.ReadI32(prefetchedCount)||prefetchedCount!=Jt_I32(count))
                        throw std::runtime_error("Invalid JT 9.x per-component colour separator profile");
                    profileKnown=true;perComponentSeparators=true;hasPrefetchedCount=true;
                }
            } else if(perComponentSeparators) {
                Jt_U32 separator;if(!reader.ReadU32(separator))throw std::runtime_error("Truncated JT 9.x per-component colour separator");
                if(c+1<components) {
                    if(!reader.ReadI32(prefetchedCount)||prefetchedCount!=Jt_I32(count))
                        throw std::runtime_error("Invalid JT 9.x per-component colour separator profile");
                    hasPrefetchedCount=true;
                }
            }
        }
        if(perComponentSeparators) {
            if(rawOverlapValid)return result.Move();
            if(lagValid)return lagResult.Move();
            throw std::runtime_error("Invalid JT 9.x per-component colour fields");
        }
        Jt_U32 hash;if(!reader.ReadU32(hash))throw std::runtime_error("Truncated JT vertex colour hash");
        // The 9.5 reference specifies raw exponent/mantissa arrays. Retain
        // that form by default, but accept the observed Lag1 form only when
        // its complete reconstructed-colour hash selects it unambiguously.
        if(lagValid&&lagHash==hash&&rawHash!=hash)return lagResult.Move();
        if(rawValid)return result.Move();
        throw std::runtime_error("Invalid JT colour exponent/mantissa fields");
    }
    Jt_U32 hash;if(!reader.ReadU32(hash))throw std::runtime_error("Truncated JT vertex colour hash");
    return result.Move();
}
}
