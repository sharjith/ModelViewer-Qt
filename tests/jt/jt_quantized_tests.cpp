// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original small quantized geometry and arithmetic fixtures; no downloaded CAD data.
#include "JtLegacyFixture.h"
#include "Jt/JtImporter.h"
#include "SceneUtils.h"
#include <JtDecode_Int32CDP.hxx>
#include <JtData_Limits.hxx>
#include <QTemporaryDir>
#include <QDir>
#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
#include <cmath>
#include <iostream>

namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F> void rejects(F action,const char* fragment=nullptr) {
    try{action();}catch(const Jt::Cancelled&){throw;}catch(const std::exception& e){
        if(fragment)check(std::string(e.what()).find(fragment)!=std::string::npos,"Wrong quantized failure diagnostic");return;
    }throw std::runtime_error("Malformed quantized fixture was accepted");
}
struct LegacyTestModel:JtData_Model {LegacyTestModel(bool big):JtData_Model(TCollection_ExtendedString()) {myMajorVersion=8;myMinorVersion=2;myIsFileLE=!big;}};
struct Reader:JtData_Reader {
    QByteArray data;size_t pos=0;
    Reader(QByteArray bytes,bool big):JtData_Reader(new LegacyTestModel(big)),data(std::move(bytes)){}
    Standard_Boolean ReadBytes(void* p,Standard_Size n)override {if(n>size_t(data.size())-pos)return false;if(n)std::memcpy(p,data.constData()+pos,n);pos+=n;return true;}
    Standard_Boolean SkipBytes(Standard_Size n)override {if(n>size_t(data.size())-pos)return false;pos+=n;return true;}
    Standard_Size GetPosition()const override{return pos;}
};
struct Bits {
    QByteArray bytes;unsigned used=0;
    void add(uint32_t value,unsigned width) {for(unsigned i=width;i>0;--i){if(used%8==0)bytes.append(char(0));bytes[bytes.size()-1]=char(uint8_t(bytes.back())|((value>>(i-1)&1)<<(7-used%8)));++used;}}
};
QByteArray arithmetic(bool big,unsigned count=5,bool escape=false,bool multi=false,unsigned frequency=1,bool loop=false,unsigned damage=0) {
    Fixture::Bytes out(big);out.u8(3);out.u8(multi?2:1);Bits bits;const unsigned occBits=frequency==65536?17:1;
    bits.add(1,32);bits.add(2,6);bits.add(occBits,6);bits.add(0,6);bits.add(multi?(damage==1?2:1):0,6);bits.add(escape?0:7,32);
    bits.add(escape?0:2,2);bits.add(damage==4?0:frequency,occBits);if(multi)bits.add(damage==1?3:1,damage==1?2:1);
    if(multi) {bits.add(1,32);bits.add(2,6);bits.add(1,6);bits.add(1,6);bits.add(0,2);bits.add(1,1);bits.add(loop?1:0,1);}
    out.data+=bits.bytes;out.u32(damage==2?0xffffffffu:escape?count+(damage==3?1:0):0);
    if(escape){out.u8(0);out.u32(count);for(unsigned i=0;i<count;++i)out.u32(10*(i+1));}
    out.u32(16);out.u32(count);if(multi)out.u32(2*count-1);out.u32(1);out.u32(0);return out.data;
}
void arithmeticTests() {
    for(bool big:{false,true}) {
        // Equal-probability symbols 10/20, code 0.34375 (binary .01011),
        // independently select 10,20,10,20 by successive interval halving.
        Fixture::Bytes bytes(big);bytes.u8(3);bytes.u8(1);Bits bits;
        bits.add(2,32);bits.add(2,6);bits.add(1,6);bits.add(4,6);bits.add(0,6);bits.add(10,32);
        bits.add(2,2);bits.add(1,1);bits.add(0,4);bits.add(3,2);bits.add(1,1);bits.add(10,4);
        bytes.data+=bits.bytes;bytes.u32(0);bytes.u32(20);bytes.u32(4);bytes.u32(1);bytes.u32(0x58000000);
        Reader r(bytes.data,big);JtDecode_Int32CDP p;check(p.Load1(r),"Binary arithmetic fixture load failed");Jt_VecI32 values(p.DecodeI32());
        for(int i=0;i<4;++i)check(values[i]==(i%2?20:10),"Arithmetic interval/renormalization mismatch");
    }
    {Jt_VecI32 values(6);for(int i=0;i<3;++i)values[i]=0;values[3]=0x7fffffff;values[4]=1;values[5]=-1;JtDecode_Unpack_Lag1(values);
     check(values[4]==int32_t(0x80000000u) && values[5]==0x7fffffff,"Legacy predictor did not wrap in 32 bits");}
    for(bool big:{false,true})for(bool escape:{false,true})for(bool multi:{false,true}) {
        if(multi && !escape)continue;
        auto bytes=arithmetic(big,5,escape,multi);Reader reader(bytes,big);JtDecode_Int32CDP packet;
        check(packet.Load1(reader) && reader.pos==size_t(bytes.size()),"Arithmetic packet envelope mismatch");
        Jt_VecI32 values(packet.DecodeI32());check(values.Count()==5,"Arithmetic output count mismatch");
        for(int i=0;i<5;++i)check(values[i]==(escape?10*(i+1):7),"Arithmetic/OOB/context output mismatch");
        for(qsizetype size=0;size<bytes.size();++size)rejects([&]{Reader r(bytes.left(size),big);JtDecode_Int32CDP p;check(p.Load1(r),"Truncated arithmetic packet");Jt_VecI32 v(p.DecodeI32());});
        for(unsigned contextCount:{0u,17u}) {auto bad=bytes;bad[1]=char(contextCount);rejects([&]{Reader r(bad,big);JtDecode_Int32CDP p;p.Load1(r);});}
        auto badWords=bytes;for(unsigned i=0;i<4;++i)badWords[badWords.size()-8+i]=0;
        rejects([&]{Reader r(badWords,big);JtDecode_Int32CDP p;p.Load1(r);},"code word count");
    }
    {Reader r(arithmetic(false,5,false,false,65536),false);JtDecode_Int32CDP p;check(p.Load1(r),"Maximum-frequency arithmetic load failed");Jt_VecI32 v(p.DecodeI32());check(v[4]==7,"Arithmetic range product overflow");}
    {Reader r(arithmetic(false,2,true,true,1,true),false);JtDecode_Int32CDP p;check(p.Load1(r),"Loop fixture load failed");rejects([&]{Jt_VecI32 v(p.DecodeI32());},"step limit");}
    for(unsigned damage:{1,2,3,4}) {
        Reader r(arithmetic(false,3,true,true,1,false,damage),false);JtDecode_Int32CDP p;
        rejects([&]{p.Load1(r);},damage==1?"next probability context":damage==4?"probability frequency":"out-of-band");
    }
    {
        Fixture::Bytes plain;plain.u8(0);plain.u32(3);plain.u32(1);plain.u32(2);plain.u32(3);auto nested=plain.data;
        for(unsigned depth=0;depth<9;++depth){auto outer=arithmetic(false,3,true);outer.replace(18,17,nested);nested=outer;}
        Reader r(nested,false);JtDecode_Int32CDP p;rejects([&]{p.Load1(r);},"recursion limit");
    }
    {
        bool decoding=false,cancelled=false;unsigned polls=0;JtData_Limits::Scope scope([&]{if(decoding && ++polls>1)throw Jt::Cancelled();});
        Reader r(arithmetic(false,5000),false);JtDecode_Int32CDP p;check(p.Load1(r),"Cancellation packet load failed");decoding=true;
        try{Jt_VecI32 v(p.DecodeI32());}catch(const Jt::Cancelled&){cancelled=true;}
        check(cancelled,"Arithmetic decoding did not poll cancellation");
    }
}
void save(const QString& path,const QByteArray& bytes) {QFile file(path);check(file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size(),"Cannot write quantized test file");}
void verify(const aiMesh& mesh) {
    check(mesh.mNumVertices==7 && mesh.mNumFaces==3 && mesh.HasNormals() && mesh.HasVertexColors(0) && mesh.HasTextureCoords(0),"Quantized geometry/attribute counts mismatch");
    const float p[7][2]={{0,0},{1,0},{0,1},{1,1},{3,0},{4,0},{3,1}};
    const unsigned indices[3][3]={{0,1,2},{1,3,2},{5,6,4}};
    for(unsigned i=0;i<7;++i) {
        if(std::abs(mesh.mVertices[i].x-p[i][0])>=1e-6 || std::abs(mesh.mVertices[i].y-p[i][1])>=1e-6 || std::abs(mesh.mVertices[i].z)>=1e-6)
            throw std::runtime_error("Quantized position "+std::to_string(i)+" differs from known coordinates: "+std::to_string(mesh.mVertices[i].x)+","+std::to_string(mesh.mVertices[i].y)+","+std::to_string(mesh.mVertices[i].z));
        check(std::abs(mesh.mNormals[i].x)<1e-6 && std::abs(mesh.mNormals[i].y)<1e-6 && std::abs(mesh.mNormals[i].z-1)<1e-6,"Known +Z Deering normal decoded incorrectly");
        check(mesh.mColors[0][i]==aiColor4D(0.25f,0.5f,0.75f,1),"Constant quantized colour mismatch");
        check(mesh.mTextureCoords[0][i]==aiVector3D(p[i][0],p[i][1],0) && mesh.mNumUVComponents[0]==2,"Quantized UV decode/copy mismatch");
    }
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)check(mesh.mFaces[i].mIndices[j]==indices[i][j],"Quantized index remapping/strip winding mismatch");
}
}
void quantizedTests() {
    arithmeticTests();QTemporaryDir temp;check(temp.isValid(),"Cannot create quantized test directory");const auto path=temp.filePath("leaf.jt");
    LegacyFixture::Options options;options.quantizedData=true;options.quantizedBits=4;options.uvBinding=1;options.colorBinding=1;
    for(unsigned minor:{0,1,2})for(bool big:{false,true})for(unsigned codec:{0,1,2}) {
        options.minorVersion=minor;options.big=big;options.codec=codec;LegacyFixture::write(path,options);
        auto result=Jt::Importer::read(path);check(result.scene->mNumMeshes==1,"Quantized fixture scene count");verify(*result.scene->mMeshes[0]);
        auto copy=std::unique_ptr<aiScene>(SceneUtils::deepCopyScene(result.scene.get()));verify(*copy->mMeshes[0]);
    }
    options.big=false;options.codec=0;
    options.quantizedColourComponents=4;LegacyFixture::write(path,options);auto rgba=Jt::Importer::read(path);verify(*rgba.scene->mMeshes[0]);options.quantizedColourComponents=3;
    for(unsigned damage=1;damage<=11;++damage) {options.quantizedDamage=damage;LegacyFixture::write(path,options);rejects([&]{Jt::Importer::read(path);});}
    options.quantizedDamage=0;LegacyFixture::write(path,options);
    auto original=LegacyFixture::bytes(options);
    for(int removed:{1,8,32,128,512}){save(path,original.left(std::max(0,int(original.size())-removed)));rejects([&]{Jt::Importer::read(path);});}
    save(path,original);
    const auto assembly=temp.filePath("assembly.jt");Fixture::write(assembly,"leaf.jt",false,true,false,true);
    auto variants=Jt::Importer::read(assembly);check(variants.scene->mNumMeshes==2,"Quantized appearance variants missing");
    for(unsigned i=0;i<2;++i)verify(*variants.scene->mMeshes[i]);
    Fixture::writeDrawStyle(assembly,"leaf.jt",0x1a);auto flat=Jt::Importer::read(assembly);
    const auto& flatMesh=*flat.scene->mMeshes[0];check(flatMesh.mNumVertices==9 && flatMesh.HasTextureCoords(0),"Flat variants dropped UVs");
    for(unsigned i=0;i<9;++i)check(flatMesh.mTextureCoords[0][i]==aiVector3D(flatMesh.mVertices[i].x,flatMesh.mVertices[i].y,0),"Flat UV corner mapping mismatch");
    std::cout<<"Quantized original fixtures and appearance/flat variants passed; checking GLB\n";
    auto source=Jt::Importer::read(path);Assimp::Exporter exporter;const auto glb=temp.filePath("quantized.glb");
    check(exporter.Export(source.scene.get(),"glb2",glb.toStdString())==AI_SUCCESS,"Quantized GLB export failed");
    Assimp::Importer importer;const auto roundtrip=importer.ReadFile(glb.toStdString(),0);
    check(roundtrip && roundtrip->mNumMeshes==1,"Quantized GLB roundtrip failed");
    const auto& restored=*roundtrip->mMeshes[0];const auto& before=*source.scene->mMeshes[0];
    check(restored.mNumFaces==3 && restored.HasNormals() && restored.HasVertexColors(0) && restored.HasTextureCoords(0),"GLB lost quantized attributes");
    // Assimp can reorder vertex records during export; compare the geometric
    // corner tuples instead of mistaking an index permutation for data loss.
    for(unsigned f=0;f<3;++f)for(unsigned c=0;c<3;++c) {
        const auto a=before.mFaces[f].mIndices[c],b=restored.mFaces[f].mIndices[c];
        check((before.mVertices[a]-restored.mVertices[b]).Length()<1e-6f && (before.mNormals[a]-restored.mNormals[b]).Length()<1e-6f,"GLB changed quantized geometry/normals");
        check((before.mTextureCoords[0][a]-restored.mTextureCoords[0][b]).Length()<1e-6f && before.mColors[0][a]==restored.mColors[0][b],"GLB changed quantized UVs/colours");
    }
    std::cout<<"Quantized 8.x: exact coordinates/normals/UVs/colours/indices, arithmetic/OOB/contexts, variants, GLB and malformed fixtures passed\n";
}
void quantizedFixtureFiles(const QString& folder) {
    check(QDir().mkpath(folder),"Cannot create quantized fixture folder");
    for(bool big:{false,true}) {
        LegacyFixture::Options options;options.minorVersion=2;options.big=big;options.quantizedData=true;options.quantizedBits=4;options.uvBinding=1;options.colorBinding=1;
        LegacyFixture::write(QDir(folder).filePath(big?"legacy_quantized_be.jt":"legacy_quantized_le.jt"),options);
    }
}

void huffmanFixtureFiles(const QString& folder) {
    check(QDir().mkpath(folder),"Cannot create Huffman fixture folder");
    for(unsigned minor:{0u,1u,2u})for(bool big:{false,true}) {
        LegacyFixture::Options o;o.minorVersion=minor;o.big=big;o.codec=2;
        LegacyFixture::write(QDir(folder).filePath(QString("huffman_8%1_%2.jt").arg(minor).arg(big?"be":"le")),o);
    }
}
