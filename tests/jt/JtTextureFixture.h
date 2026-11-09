// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original descriptor records and tiny RGB payload, not vendor CAD data.
#pragma once
#include "JtContainerFixture.h"
#include <array>
#include <vector>
#include <algorithm>
namespace TextureFixture {
struct Options {bool big=false,inlineImage=true,wrapped=false;unsigned baseVersion=1,version=1;int width=2,height=2,alignment=1,total=12,mipBytes=12,storage=-1,coordinate=2;
    unsigned pixelFormat=1,pixelType=3,inhibit=0,legacyType=2,shared=0,textureType=9,stateFlags=8,finalFields=0,imageCount=1;int channel=0,imageBorder=0,borderMode=0,wrapS=2,wrapT=2,minFilter=2,magFilter=2,blendType=2;QByteArray pixels;
    std::vector<QByteArray> extraMips;
    std::array<float,4> borderColor{0,0,0,1},blendColor{0,0,0,1};
    std::array<float,16> transform{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    std::array<unsigned,4> generation{};
    std::array<float,16> planes{};};
inline Fixture::Bytes body(Options o={}){
    Fixture::Bytes b(o.big);b.u8(o.baseVersion);b.u8(o.stateFlags);b.u32(o.inhibit);b.u32(o.finalFields);b.u8(o.version);b.u32(o.textureType);
    for(int v:{o.borderMode,o.magFilter,o.minFilter,o.wrapS,o.wrapT,2,o.blendType,0})b.u32(v);
    for(float v:o.blendColor)b.f32(v);for(float v:o.borderColor)b.f32(v);
    for(float value:o.transform)b.f32(value);
    for(auto mode:o.generation)b.u32(mode);
    for(float value:o.planes)b.f32(value);
    b.u32(o.channel);b.u32(o.coordinate);b.u32(0);b.u8(o.storage>=0?o.storage:o.inlineImage?1:0);b.u32(o.imageCount);
    if(o.imageCount&&o.inlineImage){
        b.u32(o.pixelFormat);b.u32(o.pixelType);b.u16(2);b.u16(o.alignment);b.u16(o.width);b.u16(o.height);b.u16(1);b.u16(o.imageBorder);b.u32(o.shared);b.u16(1+unsigned(o.extraMips.size()));
        int extraBytes=0;for(const auto& mip:o.extraMips)extraBytes+=mip.size();
        b.u32(o.total+extraBytes);b.u32(o.mipBytes);
        if(!o.pixels.isNull())b.data+=o.pixels;
        else {const unsigned pixels[]={255,0,0,0,255,0,0,0,255,255,255,255};for(auto v:pixels)b.u8(v);}
        for(const auto& mip:o.extraMips){b.u32(mip.size());b.data+=mip;}
    }else if(o.imageCount)b.text("original_texture.png");
    if(o.baseVersion==2)b.u32(quint32(-1));return b;
}
inline void write(const QString& path,unsigned minor,Options o={},const QString& geometry="geometry.jt"){
    using namespace Fixture;Bytes graph(o.big);element(graph,type("10dd103e"),1,partition(o.big,{}, {2}, {20}));
    element(graph,type("10dd103e"),2,partition(o.big,geometry,{}));auto data=body(o);
    if(o.wrapped){Bytes wrapped(o.big);wrapped.guid(type("10dd1073"));wrapped.u8(3);wrapped.u32(20);wrapped.append(data);element(graph,"96603dd3-5a0f-40f5-97cb-1e9647cbd37e",999,wrapped,9);}
    else element(graph,type("10dd1073"),20,data,3);
    end(graph);end(graph);graph.u16(1);graph.u32(0);container(path,graph,minor);
}
inline void writePair(const QString& path,unsigned minor,Options first,Options second,const QString& geometry="geometry.jt"){
    using namespace Fixture;Bytes graph(first.big);element(graph,type("10dd103e"),1,partition(first.big,{}, {2,3}));
    element(graph,type("10dd103e"),2,partition(first.big,geometry,{}, {20}));
    element(graph,type("10dd103e"),3,partition(first.big,geometry,{}, {21}));
    element(graph,type("10dd1073"),20,body(first),3);element(graph,type("10dd1073"),21,body(second),3);
    end(graph);end(graph);graph.u16(1);graph.u32(0);container(path,graph,minor);
}
inline Fixture::Bytes block9(Options o,unsigned block,bool stub=false){
    Fixture::Bytes b(o.big);b.u32(stub?0:o.legacyType);
    for(int v:{o.borderMode,o.magFilter,o.minFilter,o.wrapS,o.wrapT,2,o.blendType,0})b.u32(v);
    for(float v:o.blendColor)b.f32(v);for(float v:o.borderColor)b.f32(v);
    for(float value:o.transform)b.f32(value);for(auto mode:o.generation)b.u32(mode);for(float value:o.planes)b.f32(value);
    b.u32(stub?0:o.channel);b.u32(0);b.u8(o.storage>=0?o.storage:o.inlineImage?1:0);b.u32(stub?0:o.imageCount);
    if(!stub&&o.imageCount&&o.inlineImage){
        b.u32(o.pixelFormat);b.u32(o.pixelType);b.u16(2);b.u16(o.alignment);b.u16(o.width);b.u16(o.height);b.u16(1);b.u16(o.imageBorder);b.u8(o.shared);b.u16(1+unsigned(o.extraMips.size()));
        int extraBytes=0;for(const auto& mip:o.extraMips)extraBytes+=mip.size();
        b.u32(o.total+extraBytes);b.u32(o.mipBytes);
        if(!o.pixels.isNull())b.data+=o.pixels;
        else {const unsigned pixels[]={255,0,0,0,255,0,0,0,255,255,255,255};for(auto v:pixels)b.u8(v);}
        for(const auto& mip:o.extraMips){b.u32(mip.size());b.data+=mip;}
    }else if(!stub&&o.imageCount)b.text("original_texture.png");
    if(block==3)b.u32(o.coordinate);
    return b;
}
inline Fixture::Bytes body9(Options o={}){
    Fixture::Bytes b(o.big);b.u16(o.baseVersion);b.u8(o.stateFlags);b.u32(o.inhibit);b.u16(o.version);
    // Versions 2/3 contain complete empty lower-version blocks, not just a
    // replacement header. Unknown version controls need only a bounded payload.
    for(unsigned block=1;block<=std::min(o.version,3u);++block)b.append(block9(o,block,block<o.version));
    return b;
}
inline void write9(const QString& path,unsigned minor,Options o={},const QString& geometry="geometry.jt"){
    using namespace Fixture;const auto partition9=[&](const QString& reference,std::initializer_list<unsigned> children,std::initializer_list<unsigned> attrs){
        Bytes b(o.big);b.u16(1);b.u32(0);b.list(attrs);b.u16(1);b.list(children);b.u32(0);b.text(reference);
        for(unsigned i=0;i<6;++i)b.f32(0);b.f32(0);for(unsigned i=0;i<6;++i)b.u32(0);return b;
    };
    Bytes graph(o.big);element(graph,type("10dd103e"),1,partition9({}, {2}, {20}));element(graph,type("10dd103e"),2,partition9(geometry,{},{}));
    element(graph,type("10dd1073"),20,body9(o),3);end(graph);end(graph);graph.u16(1);graph.u32(0);
    const QString guid="12345678-1234-5678-9abc-def012345678";Bytes segment(o.big);segment.guid(guid);
    segment.u32(1);segment.u32(33+unsigned(graph.data.size()));segment.u32(0);segment.u32(1+unsigned(graph.data.size()));segment.u8(0);segment.append(graph);
    Bytes file(o.big);file.data=(QByteArray("Version 9.")+QByteArray::number(minor)+" JT original texture fixture").leftJustified(75,' ')+QByteArray(" \n\r\n ",5);
    file.u8(o.big?1:0);file.u32(0);file.u32(105+unsigned(segment.data.size()));file.guid(guid);file.append(segment);
    file.u32(1);file.guid(guid);file.u32(105);file.u32(unsigned(segment.data.size()));file.u32(1u<<24);
    QFile target(path);if(!target.open(QIODevice::WriteOnly)||target.write(file.data)!=file.data.size())throw std::runtime_error("Cannot write original v9 texture fixture");
}
}
