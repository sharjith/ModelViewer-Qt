// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original test-only fixture modifier. Requires a locally obtained JT 10.0 part;
// neither downloaded geometry nor reference implementation source is bundled.
#include "JtContainerFixture.h"
#include "Jt/JtImporter.h"
#include <JtDecode_MeshCoderDriver.hxx>
#include <JtDecode_VertexData.hxx>
#include <JtElement_ShapeLOD_Vertex.hxx>
#include <QCoreApplication>
#include <QDir>
#include <lzma.h>
#include <assimp/material.h>
#include <iostream>
#include <vector>

namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct FixtureModel:JtData_Model {FixtureModel():JtData_Model(TCollection_ExtendedString()){myMajorVersion=10;myMinorVersion=0;myIsFileLE=true;}};
struct Reader:JtData_Reader {
    QByteArray bytes;size_t pos=0;
    Reader(QByteArray b):JtData_Reader(new FixtureModel),bytes(std::move(b)){}
    Standard_Boolean ReadBytes(void* out,Standard_Size count)override{if(count>size_t(bytes.size())-pos)return false;std::memcpy(out,bytes.constData()+pos,count);pos+=count;return true;}
    Standard_Boolean SkipBytes(Standard_Size count)override{if(count>size_t(bytes.size())-pos)return false;pos+=count;return true;}
    Standard_Size GetPosition()const override{return pos;}
};
quint64 number(const QByteArray& b,qsizetype at,unsigned size){check(at>=0 && at+size<=b.size(),"Fixture input bounds");quint64 v=0;for(unsigned i=0;i<size;++i)v|=quint64(quint8(b[at+i]))<<(8*i);return v;}
void put(QByteArray& b,qsizetype at,quint64 value,unsigned size){check(at>=0 && at+size<=b.size(),"Fixture output bounds");for(unsigned i=0;i<size;++i)b[at+i]=char(value>>(8*i));}
QByteArray unpackGraph(const QByteArray& segment){
    check(segment.size()>=33,"Short fixture graph");const auto flag=number(segment,24,4),length=number(segment,28,4),algorithm=number(segment,32,1);
    check(length>=1 && 32+length==quint64(segment.size()),"Fixture graph envelope mismatch");
    if(flag==0)return segment.mid(33);
    check(flag==3 && algorithm==3,"Fixture generator requires LZMA or uncompressed graph");
    std::vector<uint8_t> output(64*1024*1024);size_t in=0,out=0;uint64_t memory=128*1024*1024;
    check(lzma_stream_buffer_decode(&memory,0,nullptr,reinterpret_cast<const uint8_t*>(segment.constData()+33),&in,size_t(length-1),output.data(),&out,output.size())==LZMA_OK && in==length-1,"Fixture graph decompression failed");
    return QByteArray(reinterpret_cast<const char*>(output.data()),qsizetype(out));
}
QByteArray graphSegment(const QByteArray& original,bool overrideBlue,unsigned components){
    auto data=unpackGraph(original);qsizetype pos=0;
    while(pos+25<=data.size()) {
        const auto length=number(data,pos,4);if(length==16)break;
        check(length>=21 && pos+4+length<=quint64(data.size()),"Fixture graph element bounds");
        const auto type=number(data,pos+4,4);
        if(type==0x10dd1077) {
            // The qualified TriStrip node ends in local version + U64 bindings.
            const qsizetype at=pos+4+qsizetype(length)-8;
            put(data,at,(number(data,at,8)&~quint64(0x30))|(components==3?0x10:0x20),8);
        }
        if(type==0x10dd1030 && overrideBlue) {
            const qsizetype flags=pos+25+11;const auto value=number(data,flags,2);
            check((value&1)==0,"Fixture generator requires full RGBA material fields");
            put(data,flags,value|0x20,2);
            const qsizetype diffuse=flags+2+16;put(data,diffuse,0,4);put(data,diffuse+4,0,4);put(data,diffuse+8,0x3f800000,4);
        }
        pos+=4+qsizetype(length);
    }
    Fixture::Bytes body;body.u32(0);body.u32(unsigned(data.size()+1));body.u8(0);body.data+=data;
    auto result=original.left(24)+body.data;put(result,20,result.size(),4);return result;
}
QByteArray shapeSegment(const QByteArray& original,unsigned components,unsigned quantized){
    auto data=original.mid(24);check(number(data,4,4)==0x10dd10ab,"Fixture generator requires TriStrip LOD");
    Reader reader(data);check(reader.SkipBytes(25),"Short shape element");Jt_I16 version;
    check(reader.ReadVersion(version) && version==1 && reader.ReadVersion(version) && version==1,"Unqualified fixture shape version");
    const auto outerBinding=reader.pos;Jt_U64 binding;check(reader.ReadU64(binding),"Short outer binding");
    const auto innerLength=reader.pos;check(reader.SkipBytes(25),"Short inner envelope");Jt_I32 id;
    check(reader.ReadVersion(version) && reader.ReadI32(id) && reader.ReadVersion(version),"Short fixture mesh header");
    auto mesh=JtDecode_MeshCoderDriver::LoadInputData(reader);check(mesh.operator bool(),"Cannot scan fixture topology");
    const auto innerBinding=reader.pos;check(reader.ReadU64(binding) && (binding&0x30)==0 && (binding&0x0f)==0x0a,"Fixture requires XYZ/normals and no existing colours");
    check(reader.SkipBytes(4),"Short fixture quantization parameters");Jt_I32 vertices,attributes;
    check(reader.ReadI32(vertices) && reader.ReadI32(attributes) && attributes>0 && attributes<1000000,"Invalid fixture attribute count");
    auto coords=JtDecode_VertexData::LoadCompressedCoords(reader),normals=JtDecode_VertexData::LoadCompressedNormals(reader);
    check(coords.operator bool() && normals.operator bool() && normals->GetOutVertexCount()==attributes,"Cannot scan fixture vertex arrays");
    Fixture::Bytes colors;colors.u32(attributes);colors.u8(components);colors.u8(quantized==1?9:quantized>=2?8:0);
    if(quantized>=2) {
        const bool hsv=quantized==3;colors.u8(hsv?1:0);
        for(unsigned c=0;c<4;++c) {
            const unsigned width=c==3&&components==3?0:hsv&&c==0?2:8;
            if(!hsv){colors.f32(c==3&&components==3?1.f:0.f);colors.f32(1.f);}
            colors.u8(width);
        }
        colors.u32(attributes);colors.u8(0);colors.u32(unsigned(attributes)*32);
        for(int i=0;i<attributes;++i) {
            const unsigned channel=unsigned(i/25)%3;
            uint32_t code=hsv?(channel<<16)|0xffffu:channel==0?0xff0000u:channel==1?0xff00u:0xffu;
            if(components==4)code=(code<<8)|255;
            colors.u32(code);
        }
    } else for(unsigned c=0;c<components;++c) {
        colors.u32(attributes);colors.u8(0);colors.u32(unsigned(attributes)*32);quint32 previous=0;
        for(int i=0;i<attributes;++i) {
            const float value=c==3?1.0f:float(unsigned(i/25)%3==c);quint32 bits;std::memcpy(&bits,&value,4);
            colors.u32(i<4?bits:bits-previous);previous=bits;
        }
    }
    // Hash words are placeholders: the decoder consumes but does not verify
    // coordinate/normal/colour hashes. These are display fixtures, not oracles.
    colors.u32(0);
    const auto added=colors.data.size();data.insert(qsizetype(reader.pos),colors.data);
    for(auto at:{outerBinding,innerBinding})put(data,qsizetype(at),(number(data,qsizetype(at),8)&~quint64(0x30))|(components==3?0x10:0x20),8);
    put(data,0,number(data,0,4)+quint64(added),4);put(data,qsizetype(innerLength),number(data,qsizetype(innerLength),4)+quint64(added),4);
    auto result=original.left(24)+data;put(result,20,result.size(),4);return result;
}
QByteArray fixture(const QByteArray& source,unsigned components,bool overrideBlue=false,unsigned quantized=0){
    check(source.startsWith("Version 10.0") && number(source,80,1)==0,"Fixture generator requires little-endian JT 10.0");
    auto output=source;const auto toc=number(source,85,8),count=number(source,qsizetype(toc),4);unsigned shapes=0;
    check(count>0 && count<10000,"Fixture TOC count");
    for(quint64 i=0;i<count;++i) {
        const qsizetype entry=qsizetype(toc+4+i*32);const auto offset=number(source,entry+16,8),length=number(source,entry+24,4),type=number(source,entry+28,4)>>24;
        check(offset+length<=quint64(source.size()),"Fixture segment bounds");QByteArray segment;
        if(type==1)segment=graphSegment(source.mid(qsizetype(offset),qsizetype(length)),overrideBlue,components);
        else if(type>=7 && type<=16){segment=shapeSegment(source.mid(qsizetype(offset),qsizetype(length)),components,quantized);++shapes;}
        else continue;
        put(output,entry+16,output.size(),8);put(output,entry+24,segment.size(),4);output+=segment;
    }
    check(shapes>0,"No fixture display LODs");return output;
}
void write(const QString& path,const QByteArray& data){QFile file(path);check(file.open(QIODevice::WriteOnly) && file.write(data)==data.size(),"Cannot write colour fixture");}
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    try {
        check(argc==3,"Usage: jt_color_fixturegen <locally obtained Bolt.jt> <output-folder>");
        QFile file(QString::fromLocal8Bit(argv[1]));check(file.open(QIODevice::ReadOnly),"Cannot read local sample");const auto source=file.readAll();
        const auto baseline=Jt::Importer::read(file.fileName());check(baseline.scene->mNumMeshes==1,"Fixture requires one source mesh");
        QDir output(QString::fromLocal8Bit(argv[2]));check(QDir().mkpath(output.path()),"Cannot create fixture folder");
        for(const auto& name:{"vertex_rgb.jt","vertex_rgba_opaque.jt","vertex_override_blue.jt","vertex_quantized_unsupported.jt"}) {
            const bool rgba=QString(name).contains("rgba"),blue=QString(name).contains("blue"),quantized=QString(name).contains("quantized");
            const auto path=output.filePath(name);write(path,fixture(source,rgba?4:3,blue,quantized));
            if(!quantized) {
                const auto result=Jt::Importer::read(path);check(result.scene->mNumMeshes==1,"Fixture mesh count");const auto& mesh=*result.scene->mMeshes[0];
                check(mesh.mNumVertices==913 && mesh.mNumFaces==904 && mesh.HasVertexColors(0)!=blue,"Fixture colour/geometry regression");
                const auto& original=*baseline.scene->mMeshes[0];
                for(unsigned i=0;i<mesh.mNumVertices;++i)check(mesh.mVertices[i]==original.mVertices[i] && mesh.mNormals[i]==original.mNormals[i],"Colour stream changed positions/normals");
                for(unsigned i=0;i<mesh.mNumFaces;++i)for(unsigned j=0;j<3;++j)check(mesh.mFaces[i].mIndices[j]==original.mFaces[i].mIndices[j],"Colour stream changed triangle indices");
                if(blue){aiColor4D c;check(result.scene->mMaterials[mesh.mMaterialIndex]->Get(AI_MATKEY_COLOR_DIFFUSE,c)==AI_SUCCESS && c.b==1 && c.r==0 && c.g==0,"Fixture blue override failed");}
            } else {
                bool rejected=false;try{Jt::Importer::read(path);}catch(const std::exception& e){rejected=std::string(e.what()).find("colour quantization width")!=std::string::npos;}
                check(rejected,"Quantized fixture was not rejected explicitly");
            }
        }
        Fixture::writeDrawStyle(output.filePath("vertex_rgb_flat.jt"),"vertex_rgb.jt",0x1a);
        Fixture::writeSelection(output.filePath("vertex_color_lod_fallback.jt"),"vertex_quantized_unsupported.jt","vertex_rgb.jt",true);
        {
            const auto result=Jt::Importer::read(output.filePath("vertex_color_lod_fallback.jt"));
            check(result.scene->mNumMeshes==1 && result.scene->mMeshes[0]->HasVertexColors(0) && !result.warnings.isEmpty(),"Unsupported colour alternative lost usable LOD fallback");
        }
        {
            const auto result=Jt::Importer::read(output.filePath("vertex_rgb_flat.jt"));
            check(result.scene->mNumMeshes==1 && result.scene->mMeshes[0]->HasVertexColors(0) && result.scene->mMeshes[0]->mNumVertices==2712,"Flat colour fixture regression");
        }
        const auto coloured=Jt::Importer::read(output.filePath("vertex_rgb.jt"));
        const auto& reference=*coloured.scene->mMeshes[0];
        const auto verifyPacked=[&](const QString& path){
            const auto result=Jt::Importer::read(path);check(result.scene->mNumMeshes==1,"Packed-colour fixture mesh count");const auto& mesh=*result.scene->mMeshes[0];
            check(mesh.mNumVertices==reference.mNumVertices&&mesh.mNumFaces==reference.mNumFaces&&mesh.HasVertexColors(0),"Packed colours changed mesh shape/counts");
            for(unsigned i=0;i<mesh.mNumVertices;++i)check(mesh.mVertices[i]==reference.mVertices[i]&&mesh.mNormals[i]==reference.mNormals[i]&&mesh.mColors[0][i]==reference.mColors[0][i],"Packed colours differ from independent lossless reference");
            for(unsigned i=0;i<mesh.mNumFaces;++i)for(unsigned j=0;j<3;++j)check(mesh.mFaces[i].mIndices[j]==reference.mFaces[i].mIndices[j],"Packed colours changed indices");
        };
        for(unsigned mode:{2u,3u,4u}) {
            const auto name=mode==2?"vertex_quantized_rgb.jt":mode==3?"vertex_quantized_hsv.jt":"vertex_quantized_rgba_opaque.jt";
            const auto path=output.filePath(name);write(path,fixture(source,mode==4?4:3,false,mode));verifyPacked(path);
        }
        for(unsigned minor:{0u,3u,5u,7u,8u,9u,10u}) {
            auto data=fixture(source,3,false,2);
            const auto header=(QByteArray("Version 10.")+QByteArray::number(minor)+" JT original cross-version colour fixture").leftJustified(75,' ')+QByteArray(" \n\r\n ",5);
            data.replace(0,80,header);const auto path=output.filePath(QString("vertex_quantized_rgb_10_%1.jt").arg(minor));write(path,data);verifyPacked(path);
        }
        write(output.filePath("README.txt"),
            "Keep all files together. Locally generated display fixtures; downloaded geometry is not redistributable by inference.\n"
            "vertex_rgb.jt and vertex_rgba_opaque.jt: vivid red/green/blue patches by attribute-record order, one Bolt, 913 vertices/904 triangles.\n"
            "vertex_override_blue.jt: uniformly blue Bolt; material overrides the stored vertex colours.\n"
            "vertex_rgb_flat.jt: same patches, faceted normals, 2712 vertices/904 triangles.\n"
            "vertex_quantized_unsupported.jt: expected invalid quantization-width error (deliberately malformed packet).\n"
            "vertex_quantized_rgb/vertex_quantized_hsv/vertex_quantized_rgba_opaque.jt: same patches as lossless RGB, encoded as packed quantized colours.\n"
            "vertex_quantized_rgb_10_*.jt: synthetic modern-header controls over the same local 10.0 source geometry; not authentic exports of those revisions.\n"
            "vertex_color_lod_fallback.jt: the unsupported alternative fails; one coloured Bolt loads from the other alternative.\n"
            "Colour hash words are placeholders. These fixtures exercise our native importer, not independent JT conformance.\n");
        std::cout<<output.absolutePath().toStdString()<<": native colour fixtures generated and checked\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
