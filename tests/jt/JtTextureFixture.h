// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original descriptor records and tiny RGB payload, not vendor CAD data.
#pragma once
#include "JtContainerFixture.h"
#include <array>
namespace TextureFixture {
struct Options {bool big=false,inlineImage=true,wrapped=false;unsigned baseVersion=1,version=1;int width=2,total=12,mipBytes=12,storage=-1,coordinate=2;
    std::array<float,16> transform{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};};
inline Fixture::Bytes body(Options o={}){
    Fixture::Bytes b(o.big);b.u8(o.baseVersion);b.u8(8);b.u32(0);b.u32(0);b.u8(o.version);b.u32(9);
    for(unsigned v:{0,2,2,2,2,2,2,0})b.u32(v);
    for(unsigned i=0;i<8;++i)b.f32(i%4==3?1.f:0.f);
    for(float value:o.transform)b.f32(value);
    for(unsigned i=0;i<4;++i)b.u32(0);
    for(unsigned i=0;i<16;++i)b.f32(0);
    b.u32(0);b.u32(o.coordinate);b.u32(0);b.u8(o.storage>=0?o.storage:o.inlineImage?1:0);b.u32(1);
    if(o.inlineImage){
        b.u32(1);b.u32(3);b.u16(2);b.u16(1);b.u16(o.width);b.u16(2);b.u16(1);b.u16(0);b.u32(0);b.u16(1);
        b.u32(o.total);b.u32(o.mipBytes);
        const unsigned pixels[]={255,0,0,0,255,0,0,0,255,255,255,255};for(auto v:pixels)b.u8(v);
    }else b.text("original_texture.png");
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
}
