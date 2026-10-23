// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original family-specific graph/attribute fixtures, without vendor geometry.
#pragma once
#include "JtTextureFixture.h"
#include <vector>
namespace TextureStackFixture {
struct Node {unsigned id;QString reference;std::vector<unsigned> children,attributes;};
inline void write(const QString& path,unsigned family,unsigned minor,bool big,unsigned version,
                  const std::vector<Node>& nodes,std::vector<TextureFixture::Options> attributes){
    using namespace Fixture;Bytes graph(big);
    const auto ids=[](Bytes& b,const auto& values){b.u32(unsigned(values.size()));for(auto id:values)b.u32(id);};
    for(const auto& node:nodes){Bytes b(big);
        if(family==9)b.u16(1);else b.u8(1);b.u32(0);ids(b,node.attributes);
        if(family==9)b.u16(1);else b.u8(1);ids(b,node.children);
        if(family==10)b.u8(1);b.u32(0);b.text(node.reference);
        for(unsigned i=0;i<(family==9?7u:1u);++i)b.f32(0);for(unsigned i=0;i<6;++i)b.u32(0);
        element(graph,type("10dd103e"),node.id,b);
    }
    for(size_t i=0;i<attributes.size();++i){auto o=attributes[i];o.big=big;o.version=version;
        o.baseVersion=family==10&&minor?2:1;
        if(family==9){if(o.textureType!=9)o.legacyType=o.textureType;else o.legacyType=version==1?2:9;}
        const auto body=family==9?TextureFixture::body9(o):TextureFixture::body(o);
        if(family==10&&minor){Bytes wrapped(big);wrapped.guid(type("10dd1073"));wrapped.u8(3);wrapped.u32(20+unsigned(i));wrapped.append(body);element(graph,"96603dd3-5a0f-40f5-97cb-1e9647cbd37e",1000+unsigned(i),wrapped,9);}
        else element(graph,type("10dd1073"),20+unsigned(i),body,3);
    }
    end(graph);end(graph);graph.u16(1);graph.u32(0);
    if(family==10){container(path,graph,minor);return;}
    const QString guid="12345678-1234-5678-9abc-def012345678";Bytes segment(big);segment.guid(guid);
    segment.u32(1);segment.u32(33+unsigned(graph.data.size()));segment.u32(0);segment.u32(1+unsigned(graph.data.size()));segment.u8(0);segment.append(graph);
    Bytes file(big);file.data=(QByteArray("Version 9.")+QByteArray::number(minor)+" JT original stack fixture").leftJustified(75,' ')+QByteArray(" \n\r\n ",5);
    file.u8(big?1:0);file.u32(0);file.u32(105+unsigned(segment.data.size()));file.guid(guid);file.append(segment);
    file.u32(1);file.guid(guid);file.u32(105);file.u32(unsigned(segment.data.size()));file.u32(1u<<24);
    QFile target(path);if(!target.open(QIODevice::WriteOnly)||target.write(file.data)!=file.data.size())throw std::runtime_error("Cannot write original stack fixture");
}
}
