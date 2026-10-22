// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original JT 8.0-8.2 lossless fixture serializer. Contains no downloaded CAD geometry.
#pragma once
#include "JtContainerFixture.h"
#include <zlib.h>
#include <vector>
#include <limits>
namespace LegacyFixture {
struct Options {bool big=false,compressed=true,padding=true,eofSizeQuirk=false;int badStrip=0;bool badChecksum=false,badRawSize=false,badPadding=false,quantizedData=false;float vertexAlpha=1,materialAlpha=1;unsigned materialDataFlags=0;int arithmeticSymbolCount=6;unsigned materialInhibit=0,normalBinding=1,colorBinding=0,codec=0,minorVersion=1,quantizedBits=0,uvBinding=0,quantizedDamage=0,quantizedColourComponents=3;};
inline QByteArray zlib(const QByteArray& raw) {
    uLongf size=compressBound(uLong(raw.size()));QByteArray out(qsizetype(size),'\0');
    if(compress2(reinterpret_cast<Bytef*>(out.data()),&size,reinterpret_cast<const Bytef*>(raw.constData()),uLong(raw.size()),1)!=Z_OK)throw std::runtime_error("Fixture compression failed");
    out.resize(qsizetype(size));return out;
}
inline QByteArray bytes(Options o={}) {
    using namespace Fixture;const bool big=o.big;
    const QString graphId="12345678-1234-5678-9abc-def012345678",shapeId="23456789-1234-5678-9abc-def012345678";
    Bytes graph(big),root(big);root.u32(0);root.list({20,21});root.list({2});root.u32(0);root.text("");for(int i=0;i<6;++i)root.f32(0);root.f32(0);for(int i=0;i<6;++i)root.u32(0);
    element(graph,type("10dd103e"),1,root);
    Bytes shape(big);shape.u32(0);shape.list({});for(int i=0;i<12;++i)shape.f32(0);shape.f32(1);for(int i=0;i<6;++i)shape.u32(0);shape.u32(0);shape.f32(0);
    shape.u32(o.normalBinding);shape.u32(o.uvBinding);shape.u32(o.colorBinding);for(int i=0;i<4;++i)shape.u8(0);element(graph,type("10dd1077"),2,shape);
    Bytes material(big);material.u8(8);material.u32(o.materialInhibit);material.u16(o.materialDataFlags);
    for(unsigned c=0;c<4;++c)for(unsigned j=0;j<4;++j)material.f32(j==3?(c==1?o.materialAlpha:1.0f):c==1?float(j==2):0.0f);material.f32(131072);element(graph,type("10dd1030"),20,material,3);
    Bytes transform(big);transform.u8(8);transform.u32(0);transform.u16(0x0c);transform.f32(10);transform.f32(-3);element(graph,type("10dd1083"),21,transform,3);end(graph);
    const auto string=[&](unsigned id,const char* value){Bytes b(big);b.u32(0x40);b.text(value);element(graph,type("10dd106e"),id,b);};
    string(100,"JT_PROP_NAME");string(101,"Legacy two strips");string(102,"Quad and triangle");string(103,"JT_PROP_LATE_LOADED");string(105,"Count");string(107,"Mass");
    Bytes late(big);late.u32(0);late.guid(shapeId);late.u32(7);element(graph,"e0b05be5-fbbd-11d1-a3a7-00aa00d10954",104,late);
    Bytes integer(big);integer.u32(0x40);integer.u32(quint32(-7));element(graph,type("10dd102b"),106,integer);
    Bytes real(big);real.u32(0x40);real.f32(12.5f);element(graph,type("10dd1019"),108,real);end(graph);
    graph.u16(1);graph.u32(2);graph.u32(1);for(auto p:{std::pair<unsigned,unsigned>{100,101},{105,106},{107,108}}){graph.u32(p.first);graph.u32(p.second);}graph.u32(0);
    graph.u32(2);graph.u32(100);graph.u32(102);graph.u32(103);graph.u32(104);graph.u32(0);
    Bytes raw(big);const float points[7][3]={{0,0,0},{1,0,0},{0,1,0},{1,1,0},{3,0,0},{4,0,0},{3,1,0}};
    for(unsigned i=0;i<7;++i){if(o.normalBinding) {raw.f32(0);raw.f32(0);raw.f32(i==4?0:1);}for(float value:points[i])raw.f32(value);}
    auto payload=o.compressed?zlib(raw.data):raw.data;
    if(o.compressed && o.badChecksum)payload[payload.size()-1]=char(payload.back()^1);
    if(o.compressed && o.padding)payload.append(o.badPadding?'\1':'\0');
    Bytes lod(big);lod.u16(1);lod.u8(o.normalBinding);lod.u8(o.uvBinding);lod.u8(o.colorBinding);lod.u8(0);for(int i=0;i<4;++i)lod.u8(i==0?o.quantizedBits:0);lod.u16(1);lod.u16(1);lod.u8(o.normalBinding);lod.u8(o.uvBinding);lod.u8(o.colorBinding);for(int i=0;i<4;++i)lod.u8(i==0?o.quantizedBits:0);
    lod.u8(o.codec);
    if(o.codec==2) {
        lod.u8(1);QByteArray context;unsigned used=0;
        const auto bits=[&](quint32 value,unsigned width){for(unsigned i=width;i>0;--i){if(used%8==0)context.append(char(0));context[context.size()-1]=char(quint8(context.back())|(((value>>(i-1))&1)<<(7-used%8)));++used;}};
        bits(3,32);bits(3,6);bits(3,6);bits(3,6);bits(0,6);bits(0,32);
        // Weights 1/2/4, values 0/4/7: codes 11/10/0; strip boundaries 0,4,7.
        bits(2,3);bits(1,3);bits(0,3);bits(3,3);bits(2,3);bits(4,3);bits(4,3);bits(4,3);bits(7,3);
        lod.data+=context;lod.u32(0);lod.u32(5);lod.u32(3);lod.u32(1);lod.u32(0xe0000000);
    } else if(o.codec==3) {
        // Singleton escape alphabets alternate contexts. Only context 0 emits
        // an OOB value; the final context-1 escape follows the final output.
        lod.u8(2);QByteArray context;unsigned used=0;
        const auto bits=[&](quint32 value,unsigned width){for(unsigned i=width;i>0;--i){if(used%8==0)context.append(char(0));context[context.size()-1]=char(quint8(context.back())|(((value>>(i-1))&1)<<(7-used%8)));++used;}};
        bits(1,32);bits(0,6);bits(1,6);bits(0,6);bits(1,6);bits(0,32);bits(1,1);bits(1,1);
        bits(1,32);bits(0,6);bits(1,6);bits(1,6);bits(1,1);bits(0,1);
        lod.data+=context;lod.u32(3);lod.u8(0);lod.u32(3);lod.u32(0);lod.u32(4);lod.u32(7);
        lod.u32(32);lod.u32(3);lod.u32(quint32(o.arithmeticSymbolCount));lod.u32(1);lod.u32(0);
    } else if(o.codec==1) {
        const char bits[]="01110010000111";quint32 word=0;for(unsigned i=0;i<sizeof(bits)-1;++i)word|=quint32(bits[i]=='1')<<(31-i);
        lod.u32(sizeof(bits)-1);lod.u32(3);lod.u32(1);lod.u32(word);
    } else {lod.u32(3);lod.u32(0);lod.u32(o.badStrip==1?8:4);lod.u32(o.badStrip==2?3:7);}
    if(o.quantizedData) {
        const auto packet=[&](const std::vector<int>& values,unsigned count=7) {lod.u8(0);lod.u32(count);for(int v:values)lod.u32(quint32(v));};
        const auto quantizer=[&](float lo,float hi,unsigned bits) {lod.f32(lo);lod.f32(hi);lod.u8(bits);};
        // Four-bit quantization over [-0.5,15.5] gives exact integer points:
        // code k decodes to k-1. First four predictor values are literal.
        for(unsigned c=0;c<3;++c)quantizer(o.quantizedDamage==5?std::numeric_limits<float>::quiet_NaN():-0.5f,15.5f,o.quantizedDamage==6?25:4);
        lod.u32(o.quantizedDamage==1?8:7);
        packet({1,2,1,2,2,1,-1});packet({1,1,2,2,-1,0,1});packet({1,1,1,1,0,0,0});
        if(o.normalBinding) {
            lod.u8(2);lod.u32(o.quantizedDamage==2?6:7);
            packet({o.quantizedDamage==7?6:1,1,1,1,0,0,0});packet({7,7,7,7,0,0,0});
            packet({o.quantizedDamage==11?-1:3,3,3,3,0,0,0});packet({0,0,0,0,0,0,0});
        }
        if(o.uvBinding) {
            quantizer(-0.5f,15.5f,4);quantizer(-0.5f,15.5f,4);lod.u8(4);
            packet({1,2,1,2,2,1,-1},o.quantizedDamage==3?6:7);packet({1,1,2,2,-1,0,1});
        }
        if(o.colorBinding) {
            lod.u8(o.quantizedDamage==9?1:0);
            quantizer(0.25f,o.quantizedDamage==8?0.75f:0.25f,8);quantizer(0.5f,0.5f,8);quantizer(0.75f,0.75f,8);quantizer(o.vertexAlpha,o.vertexAlpha,0);
            lod.u8(8);lod.u8(o.quantizedColourComponents);lod.u8(o.quantizedDamage==10?1:0);packet({0,0,0,0,0,0,0});
        }
        packet({0,1,2,3,1,1,o.quantizedDamage==4?77:-4});
    } else {
        lod.u32(unsigned(raw.data.size())+(o.badRawSize?4:0));lod.u32(o.compressed?unsigned(payload.size()):0);lod.data+=payload;
    }
    Bytes shapeData(big);shapeData.u32(17+unsigned(lod.data.size()));shapeData.guid(type("10dd10ab"));shapeData.u8(0);shapeData.append(lod);
    Bytes shapeSegment(big);shapeSegment.guid(shapeId);shapeSegment.u32(7);shapeSegment.u32(24+unsigned(shapeData.data.size()));shapeSegment.append(shapeData);
    const auto zipped=zlib(graph.data);Bytes graphSegment(big);graphSegment.guid(graphId);graphSegment.u32(1);const unsigned graphLength=33+unsigned(zipped.size())-(o.eofSizeQuirk?1:0);graphSegment.u32(graphLength);graphSegment.u32(2);graphSegment.u32(1+unsigned(zipped.size()));graphSegment.u8(2);graphSegment.data+=zipped;
    constexpr unsigned toc=105,shapeOffset=toc+4+2*28;const unsigned graphOffset=shapeOffset+unsigned(shapeSegment.data.size());
    Bytes file(big);file.data=(QByteArray("Version 8.")+QByteArray::number(o.minorVersion)+" JT original regression fixture").leftJustified(75,' ')+QByteArray(" \n\r\n ",5);file.u8(big?1:0);file.u32(0);file.u32(toc);file.guid(graphId);file.u32(2);
    file.guid(graphId);file.u32(graphOffset);file.u32(graphLength);file.u32(1u<<24);file.guid(shapeId);file.u32(shapeOffset);file.u32(unsigned(shapeSegment.data.size()));file.u32(7u<<24);
    file.append(shapeSegment);file.append(graphSegment);return file.data;
}
inline void write(const QString& path,Options options={}){QFile file(path);const auto data=bytes(options);if(!file.open(QIODevice::WriteOnly)||file.write(data)!=data.size())throw std::runtime_error("Cannot write legacy fixture");}
}
