// Original ModelViewer-Qt addition, 2026-10-19.
// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <JtDecode_VertexData.hxx>
#include <JtData_Limits.hxx>
#include <cmath>
#include <cstring>

// JT 10.0 sections 7.1.2.3 and 13.1.6. Initially qualifies only the
// lossless IEEE-754 component path; quantized RGB/HSV is rejected explicitly.
namespace JtDecode_VertexColors {
inline JtDecode_VertexData::Decoded::Mover Read(JtData_Reader& reader,
                                                unsigned components,int expectedCount) {
    Jt_U32 count;Jt_U8 storedComponents,bits;
    if(!reader.ReadU32(count) || !reader.ReadU8(storedComponents) || !reader.ReadU8(bits))
        throw std::runtime_error("Truncated JT vertex colour header");
    if(count>16000000 || expectedCount<0 || count!=unsigned(expectedCount)
       || (components!=3 && components!=4) || storedComponents!=components)
        throw std::runtime_error("Invalid JT vertex colour count/components");
    if(bits) throw std::runtime_error("Unsupported JT quantized vertex colours");
    JtData_Limits::allocation(size_t(count)*components*sizeof(float));
    JtDecode_VertexData::Decoded result{int(count),int(components)};
    for(unsigned c=0;c<components;++c) {
        JtDecode_Int32CDP packet;
        if(!packet.Load2(reader) || packet.GetOutValCount()!=int(count))
            throw std::runtime_error("Invalid JT vertex colour packet");
        Jt_VecI32 values(packet.DecodeU32(JtDecode_Unpack_Lag1));
        if(values.Count()!=int(count)) throw std::runtime_error("Invalid decoded JT vertex colour count");
        for(unsigned i=0;i<count;++i) {
            float value;std::memcpy(&value,&values[int(i)],sizeof(value));
            if(!std::isfinite(value) || value<0 || value>1)
                throw std::runtime_error("JT vertex colour component is outside [0,1]");
            result[int(i)][int(c)]=value;
        }
    }
    Jt_U32 hash;
    if(!reader.ReadU32(hash)) throw std::runtime_error("Truncated JT vertex colour hash");
    return result.Move();
}
}
