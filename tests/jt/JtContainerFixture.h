// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original minimal JT 10.0 container fixtures, generated without sample geometry.
#pragma once
#include <QByteArray>
#include <QFile>
#include <QString>
#include <QUuid>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <utility>
namespace Fixture {
struct Bytes {
    QByteArray data;bool big=false;
    explicit Bytes(bool b=false):big(b){}
    void u8(unsigned v){data.append(char(v));}
    void number(quint64 v,int size){for(int i=0;i<size;++i)u8(unsigned(v>>(8*(big?size-1-i:i)))&255);}
    void u16(unsigned v){number(v,2);}void u32(quint32 v){number(v,4);}void u64(quint64 v){number(v,8);}
    void f32(float v){quint32 b;std::memcpy(&b,&v,4);u32(b);}void f64(double v){quint64 b;std::memcpy(&b,&v,8);u64(b);}
    void text(const QString& v){u32(quint32(v.size()));for(QChar c:v)u16(c.unicode());}
    void guid(const QString& text){const QUuid g(text);u32(g.data1);u16(g.data2);u16(g.data3);data.append(reinterpret_cast<const char*>(g.data4),8);}
    void list(std::initializer_list<unsigned> ids){u32(unsigned(ids.size()));for(auto i:ids)u32(i);}
    void append(const Bytes& b){data+=b.data;}
};
inline QString type(const char* first){return QString::fromLatin1(first)+"-2ac8-11d1-9b6b-0080c7bb5997";}
inline void element(Bytes& graph,const QString& guid,unsigned id,const Bytes& body,unsigned base=0){graph.u32(21+quint32(body.data.size()));graph.guid(guid);graph.u8(base);graph.u32(id);graph.append(body);}
inline void end(Bytes& stream){stream.u32(16);stream.guid("ffffffff-ffff-ffff-ffff-ffffffffffff");}
inline void baseNode(Bytes& b,std::initializer_list<unsigned> attributes){b.u8(1);b.u32(0);b.list(attributes);}
inline Bytes partition(bool big,const QString& reference,std::initializer_list<unsigned> children){
    Bytes b(big);baseNode(b,{});b.u8(1);b.list(children);b.u8(1);b.u32(0);b.text(reference);b.f32(0);for(int i=0;i<6;++i)b.u32(0);return b;
}
inline void container(const QString& path,const Bytes& graph) {
    const QString segmentId="12345678-1234-5678-9abc-def012345678";
    const bool big=graph.big;
    Bytes segment(big);segment.guid(segmentId);segment.u32(1);segment.u32(quint32(33+graph.data.size()));segment.u32(0);segment.u32(quint32(graph.data.size()+1));segment.u8(0);segment.append(graph);
    Bytes file(big);file.data=QByteArray("Version 10.0 JT synthetic interoperability test").leftJustified(80,' ');file.u8(big?1:0);file.u32(0);file.u64(109+quint64(segment.data.size()));file.guid(segmentId);file.append(segment);
    file.u32(1);file.guid(segmentId);file.u64(109);file.u32(quint32(segment.data.size()));file.u32(1u<<24);
    QFile target(path);if(!target.open(QIODevice::WriteOnly)||target.write(file.data)!=file.data.size())throw std::runtime_error("Cannot write synthetic JT container");
}
inline void writeSelection(const QString& path,const QString& first,const QString& second,bool range=false,unsigned selected=0,bool big=false,unsigned version=1,std::initializer_list<float> limits={10}) {
    Bytes graph(big);element(graph,type("10dd103e"),1,partition(big,QString(),{10}));
    element(graph,type("10dd103e"),2,partition(big,first,{}));element(graph,type("10dd103e"),3,partition(big,second,{}));
    Bytes choice(big);baseNode(choice,{});choice.u8(1);choice.list({2,3});
    if(range) {choice.u8(1);choice.u8(version);choice.u32(unsigned(limits.size()));for(float v:limits)choice.f32(v);choice.f32(0);choice.f32(0);choice.f32(0);}
    else {choice.u8(version);choice.u32(selected);}
    element(graph,type(range?"10dd104c":"10dd10f3"),10,choice);
    end(graph);end(graph);graph.u16(1);graph.u32(0);container(path,graph);
}
struct MetadataOptions {bool big=false;unsigned integerVersion=1,baseVersion=1,tableVersion=1,stringVersion=1,floatVersion=1;bool duplicateTable=false;};
inline void writeMetadata(const QString& path,const QString& reference=QString(),MetadataOptions options={}) {
    const bool big=options.big;
    Bytes graph(big);element(graph,type("10dd103e"),1,partition(big,QString(),{2}));
    element(graph,type("10dd103e"),2,partition(big,reference,{}));end(graph);
    const auto string=[&](unsigned id,const QString& value) {
        Bytes b(big);b.u8(options.baseVersion);b.u32(0x45);b.u8(options.stringVersion);b.text(value);element(graph,type("10dd106e"),id,b);
    };
    string(100,"JT_PROP_NAME");string(101,"Metadata assembly;original-id");
    string(102,"PartNumber");string(103,QString::fromUtf8("\xe9\xbd\xbf\xe8\xbd\xae\xe2\x9a\x99"));
    string(104,"Count");string(106,"Mass");string(108,"Repeated");string(109,"first");string(110,"second");
    string(111,"Unsupported");string(113,"LongText");string(114,QString(1100,'x'));string(115,"Nonfinite");
    string(117,"child value");
    Bytes integer(big);integer.u8(options.baseVersion);integer.u32(0xa5);integer.u8(options.integerVersion);integer.u32(quint32(-7));element(graph,type("10dd102b"),105,integer);
    Bytes real(big);real.u8(1);real.u32(0x5a);real.u8(options.floatVersion);real.f32(12.5f);element(graph,type("10dd1019"),107,real);
    Bytes nan(big);nan.u8(1);nan.u32(0);nan.u8(1);nan.u32(0x7fc00000);element(graph,type("10dd1019"),116,nan);
    Bytes unknown(big);element(graph,type("deadbeef"),112,unknown);end(graph);
    graph.u16(options.tableVersion);graph.u32(options.duplicateTable?3:2);
    graph.u32(1);
    for(auto pair:{std::pair<unsigned,unsigned>{100,101},{102,103},{104,105},{106,107},{108,109},{108,110},{111,112},{113,114},{115,116}}) {graph.u32(pair.first);graph.u32(pair.second);}
    graph.u32(0);graph.u32(2);graph.u32(108);graph.u32(117);graph.u32(0);
    if(options.duplicateTable) {graph.u32(1);graph.u32(0);}
    container(path,graph);
}
inline void write(const QString& path,const QString& reference,bool big=false,bool assembly=true,bool duplicate=false){
    Bytes graph(big);element(graph,type("10dd103e"),1,partition(big,assembly?QString():reference,assembly?std::initializer_list<unsigned>{10,11}:std::initializer_list<unsigned>{}));
    if(assembly){
        element(graph,type("10dd103e"),2,partition(big,reference,{}));
        for(unsigned i=0;i<2;++i){
            Bytes instance(big);baseNode(instance,{20+2*i,21+2*i});instance.u8(1);instance.u32(2);element(graph,type("10dd102a"),10+i,instance);
            Bytes transform(big);transform.u8(1);transform.u8(8);transform.u32(0);transform.u32(0);transform.u8(1);transform.u16(8);transform.f64(10+10*i);element(graph,type("10dd1083"),20+2*i,transform,3);
            Bytes material(big);material.u8(1);material.u8(8);material.u32(0);material.u32(1u<<6);material.u8(1);material.u16(0x3980);
            for(unsigned color=0;color<4;++color)for(unsigned c=0;c<4;++c) material.f32(c==3?1.0f:color==1?float(c==i):0.0f);
            material.f32(15);material.f32(0);material.f32(1);element(graph,type("10dd1030"),21+2*i,material,3);
        }
    }
    if(duplicate)element(graph,type("10dd103e"),1,partition(big,QString(),{}));
    end(graph);
    const QString names[]={"JT_PROP_NAME","assembly","occurrence A","occurrence B"};
    for(unsigned i=0;i<4;++i){Bytes string(big);string.u8(1);string.u32(0);string.u8(1);string.text(names[i]);element(graph,type("10dd106e"),100+i,string);}
    end(graph);graph.u16(1);graph.u32(assembly?3:1);
    graph.u32(1);graph.u32(100);graph.u32(101);graph.u32(0);
    if(assembly)for(unsigned i=0;i<2;++i){graph.u32(10+i);graph.u32(100);graph.u32(102+i);graph.u32(0);}
    container(path,graph);
}
}
