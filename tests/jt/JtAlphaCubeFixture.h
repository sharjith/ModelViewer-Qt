// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original nested cubes using the already qualified legacy fixture layout.
#pragma once
#include "JtLegacyFixture.h"
namespace AlphaCubeFixture {
inline QByteArray bytes(unsigned minor,bool big,float alpha,unsigned flags) {
    using namespace Fixture;
    const QString graphId="12345678-1234-5678-9abc-def012345678";
    const QString shapeIds[2]={"23456789-1234-5678-9abc-def012345678","34567890-1234-5678-9abc-def012345678"};
    Bytes graph(big),root(big);root.u32(0);root.list({});root.list({2,3});root.u32(0);root.text("");
    for(int i=0;i<6;++i)root.f32(0);root.f32(0);for(int i=0;i<6;++i)root.u32(0);
    element(graph,type("10dd103e"),1,root);
    for(unsigned mesh=0;mesh<2;++mesh){
        Bytes shape(big);shape.u32(0);shape.list({20+mesh});
        for(int i=0;i<12;++i)shape.f32(0);shape.f32(1);for(int i=0;i<6;++i)shape.u32(0);shape.u32(0);shape.f32(0);
        shape.u32(0);shape.u32(0);shape.u32(1);for(int i=0;i<4;++i)shape.u8(0);element(graph,type("10dd1077"),2+mesh,shape);
        Bytes material(big);material.u8(8);material.u32(0);material.u16(mesh?0x3980:flags);
        for(unsigned c=0;c<4;++c)for(unsigned j=0;j<4;++j)material.f32(j==3?1.f:0.f);
        material.f32(16);element(graph,type("10dd1030"),20+mesh,material,3);
    }
    end(graph);
    const auto string=[&](unsigned id,const char* value){Bytes b(big);b.u32(0x40);b.text(value);element(graph,type("10dd106e"),id,b);};
    string(100,"JT_PROP_NAME");string(101,"Legacy two strips");string(102,"Quad and triangle");
    string(103,"JT_PROP_LATE_LOADED");string(106,"Opaque inner cube");
    for(unsigned mesh=0;mesh<2;++mesh){Bytes late(big);late.u32(0);late.guid(shapeIds[mesh]);late.u32(7);element(graph,"e0b05be5-fbbd-11d1-a3a7-00aa00d10954",104+mesh,late);}
    end(graph);graph.u16(1);graph.u32(3);
    graph.u32(1);graph.u32(100);graph.u32(101);graph.u32(0);
    for(unsigned mesh=0;mesh<2;++mesh){graph.u32(2+mesh);graph.u32(100);graph.u32(mesh?106:102);graph.u32(103);graph.u32(104+mesh);graph.u32(0);}
    // Six outward-facing quad strips. Face-local vertices retain sharp edges.
    const int corners[24][3]={
        {-1,-1,1},{1,-1,1},{-1,1,1},{1,1,1},
        {1,-1,-1},{-1,-1,-1},{1,1,-1},{-1,1,-1},
        {1,-1,1},{1,-1,-1},{1,1,1},{1,1,-1},
        {-1,-1,-1},{-1,-1,1},{-1,1,-1},{-1,1,1},
        {-1,1,1},{1,1,1},{-1,1,-1},{1,1,-1},
        {-1,-1,-1},{1,-1,-1},{-1,-1,1},{1,-1,1}};
    QByteArray segments[2];
    for(unsigned mesh=0;mesh<2;++mesh){
        Bytes lod(big);lod.u16(1);lod.u8(0);lod.u8(0);lod.u8(1);lod.u8(0);
        for(int i=0;i<4;++i)lod.u8(i==0?4:0);
        lod.u16(1);lod.u16(1);lod.u8(0);lod.u8(0);lod.u8(1);for(int i=0;i<4;++i)lod.u8(i==0?4:0);
        const auto packet=[&](const std::vector<int>& values){lod.u8(0);lod.u32(unsigned(values.size()));for(int v:values)lod.u32(quint32(v));};
        packet({0,4,8,12,0,0,0}); // Stride1 predicts the remaining quad boundaries
        const auto quantizer=[&](float lo,float hi,unsigned bits){lod.f32(lo);lod.f32(hi);lod.u8(bits);};
        for(int axis=0;axis<3;++axis)quantizer(-4.5f,11.5f,4);lod.u32(24);
        for(int axis=0;axis<3;++axis){
            std::vector<int> codes,delta;for(const auto& p:corners)codes.push_back(p[axis]*(mesh?2:4)+5);
            for(size_t i=0;i<codes.size();++i)delta.push_back(codes[i]-(i<4?0:codes[i-1]));packet(delta);
        }
        lod.u8(0); // constant RGB(A) quantizers, not packed colours
        const float rgb[2][3]={{0.1f,0.35f,0.9f},{1.f,0.55f,0.05f}};
        for(float c:rgb[mesh])quantizer(c,c,8);quantizer(mesh?1.f:alpha,mesh?1.f:alpha,0);
        lod.u8(8);lod.u8(4);lod.u8(0);packet(std::vector<int>(24,0));
        std::vector<int> indices(24,0);for(int i=0;i<4;++i)indices[i]=i;packet(indices); // StripIdx predicts i after four literals
        Bytes data(big);data.u32(17+unsigned(lod.data.size()));data.guid(type("10dd10ab"));data.u8(0);data.append(lod);
        Bytes segment(big);segment.guid(shapeIds[mesh]);segment.u32(7);segment.u32(24+unsigned(data.data.size()));segment.append(data);segments[mesh]=segment.data;
    }
    const auto zipped=LegacyFixture::zlib(graph.data);Bytes graphSegment(big);graphSegment.guid(graphId);graphSegment.u32(1);
    graphSegment.u32(33+unsigned(zipped.size()));graphSegment.u32(2);graphSegment.u32(1+unsigned(zipped.size()));graphSegment.u8(2);graphSegment.data+=zipped;
    constexpr unsigned toc=105,firstShape=toc+4+3*28;
    const unsigned graphOffset=firstShape+unsigned(segments[0].size()+segments[1].size());
    Bytes file(big);file.data=(QByteArray("Version 8.")+QByteArray::number(minor)+" JT original nested cube fixture").leftJustified(75,' ')+QByteArray(" \n\r\n ",5);
    file.u8(big?1:0);file.u32(0);file.u32(toc);file.guid(graphId);file.u32(3);
    file.guid(graphId);file.u32(graphOffset);file.u32(unsigned(graphSegment.data.size()));file.u32(1u<<24);
    unsigned offset=firstShape;for(unsigned mesh=0;mesh<2;++mesh){file.guid(shapeIds[mesh]);file.u32(offset);file.u32(unsigned(segments[mesh].size()));file.u32(7u<<24);offset+=unsigned(segments[mesh].size());}
    file.data+=segments[0];file.data+=segments[1];file.append(graphSegment);return file.data;
}
inline void write(const QString& path,unsigned minor,bool big,float alpha,unsigned flags){
    QFile file(path);const auto data=bytes(minor,big,alpha,flags);
    if(!file.open(QIODevice::WriteOnly)||file.write(data)!=data.size())throw std::runtime_error("Cannot write nested cube fixture");
}
}
