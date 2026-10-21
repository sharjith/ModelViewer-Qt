// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#include "JtLegacyFixture.h"
#include "Jt/JtImporter.h"
#include <QDir>
#include <QTemporaryDir>
#include <assimp/material.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <algorithm>
namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void save(const QString& path,const QByteArray& bytes){QFile f(path);check(f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size(),"Cannot write legacy mutation");}
void rejects(const QString& path){bool rejected=false;try{Jt::Importer::read(path);}catch(const Jt::Cancelled&){throw;}catch(const std::exception&){rejected=true;}check(rejected,"Legacy malformed fixture was accepted");}
}
void legacyTests(){
    QTemporaryDir temp;check(temp.isValid(),"Cannot create legacy test directory");const auto path=temp.filePath("legacy.jt");
    LegacyFixture::write(path,{});
    Jt::Options tinyBudget;tinyBudget.decodedAllocationBudget=1;
    bool budgetRejected=false;
    try{Jt::Importer::read(path,tinyBudget);}catch(const std::runtime_error& e){budgetRejected=std::string(e.what()).find("memory limit")!=std::string::npos;}
    check(budgetRejected,"Importer ignored configured allocation budget");
    for(unsigned minor:{0,1,2})for(bool big:{false,true})for(bool compressed:{false,true})for(bool quirk:{false,true}) {
        if(minor!=1 && quirk)continue; // Warehouse envelope exception is qualified only for 8.1.
        LegacyFixture::Options o;o.minorVersion=minor;o.big=big;o.compressed=compressed;o.eofSizeQuirk=quirk;LegacyFixture::write(path,o);
        auto result=Jt::Importer::read(path);check(result.scene->mNumMeshes==1,"Legacy scene mesh count");const auto& mesh=*result.scene->mMeshes[0];
        check(mesh.mNumVertices==7 && mesh.mNumFaces==3,"Legacy strip counts");
        const unsigned expected[3][3]={{0,1,2},{1,3,2},{4,5,6}};
        for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)check(mesh.mFaces[i].mIndices[j]==expected[i][j],"Strip winding did not reset");
        for(unsigned i=0;i<7;++i)check(std::abs(mesh.mNormals[i].z-1)<1e-6f,"Legacy normal decode/reconstruction failed");
        check(result.scene->mRootNode->mName==aiString("Legacy two strips"),"Legacy string property failed");
        check(result.scene->mRootNode->mTransformation.a4==10 && result.scene->mRootNode->mTransformation.b4==-3,"Legacy F32 transform layout failed");
        check(result.scene->mRootNode->mMetaData && result.scene->mRootNode->mMetaData->HasKey("JTProperties"),"Legacy scalar properties lost");
        aiMetadata records,integerRecord,floatRecord;int32_t integer=0;float real=0;
        check(result.scene->mRootNode->mMetaData->Get("JTProperties",records) && records.Get(1,integerRecord) && records.Get(2,floatRecord)
              && integerRecord.Get("Value",integer) && integer==-7 && floatRecord.Get("Value",real) && real==12.5f,"Legacy typed scalar values shifted");
        float exponent=0;aiColor4D diffuse;const auto& mat=*result.scene->mMaterials[mesh.mMaterialIndex];
        check(mat.Get(AI_MATKEY_SHININESS,exponent)==AI_SUCCESS && exponent==131072 && mat.Get(AI_MATKEY_COLOR_DIFFUSE,diffuse)==AI_SUCCESS && diffuse.b==1,"Legacy material fields shifted");
    }
    for(bool big:{false,true}){LegacyFixture::Options o;o.big=big;o.codec=1;LegacyFixture::write(path,o);auto r=Jt::Importer::read(path);check(r.scene->mMeshes[0]->mNumFaces==3,"Legacy bitlength packet failed");}
    {LegacyFixture::Options o;o.padding=false;LegacyFixture::write(path,o);auto r=Jt::Importer::read(path);check(r.scene->mMeshes[0]->mNumFaces==3,"Unpadded legacy zlib block failed");}
    {LegacyFixture::Options o;o.codec=4;LegacyFixture::write(path,o);rejects(path);}
    for(unsigned mode=0;mode<6;++mode){LegacyFixture::Options o;o.badStrip=mode<2?int(mode+1):0;o.badChecksum=mode==2;o.badRawSize=mode==3;o.colorBinding=mode==4?1:0;o.badPadding=mode==5;LegacyFixture::write(path,o);rejects(path);}
    LegacyFixture::Options noNormals;noNormals.normalBinding=0;LegacyFixture::write(path,noNormals);auto generated=Jt::Importer::read(path);check(generated.scene->mMeshes[0]->mNormals[6].z==1,"Legacy absent normals not generated");
    const auto original=LegacyFixture::bytes();
    auto badGraph=original;badGraph[badGraph.size()-1]=char(badGraph.back()^1);save(path,badGraph);rejects(path);
    for(int removed:{1,2,8,32,128,512}){save(path,original.left(std::max(0,int(original.size())-removed)));rejects(path);}
    // Both endian byte order flags and the EOF size exception are deliberately
    // qualified; the exception must not hide extra bytes after a short segment.
    LegacyFixture::Options quirk;quirk.eofSizeQuirk=true;auto padded=LegacyFixture::bytes(quirk);padded.append('\0');save(path,padded);rejects(path);
    for(unsigned minor:{0,2}) {LegacyFixture::Options o;o.minorVersion=minor;o.eofSizeQuirk=true;LegacyFixture::write(path,o);rejects(path);}
    for(unsigned minor:{0,1,2}) {
        // Deliberately unsupported encoding header: every family member must
        // diagnose the encoding rather than reject a known file version.
        LegacyFixture::Options o;o.minorVersion=minor;o.quantizedBits=25;o.colorBinding=1;LegacyFixture::write(path,o);
        bool rejectedQuantized=false;try{Jt::Importer::read(path);}catch(const std::runtime_error& e){rejectedQuantized=std::string(e.what()).find("JT 8.x position quantization width")!=std::string::npos;}
        check(rejectedQuantized,"8.x family lost its quantized-encoding diagnostic");
    }
    for(unsigned minor:{0,2}) {
        for(bool big:{false,true}) {LegacyFixture::Options o;o.minorVersion=minor;o.big=big;o.codec=1;LegacyFixture::write(path,o);auto r=Jt::Importer::read(path);check(r.scene->mMeshes[0]->mNumFaces==3,"8.x family bitlength packet failed");}
        for(unsigned mode=0;mode<6;++mode) {LegacyFixture::Options o;o.minorVersion=minor;o.badStrip=mode<2?int(mode+1):0;o.badChecksum=mode==2;o.badRawSize=mode==3;o.colorBinding=mode==4?1:0;o.codec=mode==5?4:0;LegacyFixture::write(path,o);rejects(path);}
    }
    std::cout<<"JT 8.0-8.2 lossless family: native LE/BE, strips, material/metadata, zlib and malformed fixtures passed\n";
}
void legacyFixtureFiles(const QString& folder){
    check(QDir().mkpath(folder),"Cannot create legacy fixture folder");
    for(bool big:{false,true}){LegacyFixture::Options o;o.big=big;LegacyFixture::write(QDir(folder).filePath(big?"legacy8_two_strips_be.jt":"legacy8_two_strips_le.jt"),o);}
}
void warehouseTests(const QString& path){
    auto result=Jt::Importer::read(path);unsigned vertices=0,triangles=0;double lo[3]={1e300,1e300,1e300},hi[3]={-1e300,-1e300,-1e300};
    for(unsigned i=0;i<result.scene->mNumMeshes;++i){const auto& m=*result.scene->mMeshes[i];vertices+=m.mNumVertices;triangles+=m.mNumFaces;
        for(unsigned j=0;j<m.mNumVertices;++j){check(std::abs(m.mNormals[j].Length()-1)<1e-4,"Warehouse normal invalid");for(unsigned k=0;k<3;++k){check(std::isfinite(m.mVertices[j][k]),"Warehouse nonfinite coordinate");lo[k]=std::min(lo[k],double(m.mVertices[j][k]));hi[k]=std::max(hi[k],double(m.mVertices[j][k]));}}
        for(unsigned j=0;j<m.mNumFaces;++j)for(unsigned k=0;k<3;++k)check(m.mFaces[j].mIndices[k]<m.mNumVertices,"Warehouse bad index");
    }
    check(result.scene->mNumMeshes==1084 && vertices==602484 && triangles==200828,"Warehouse geometry regression");
    Jt::Options options;unsigned polls=0;options.cancelled=[&]{return ++polls>300;};bool cancelled=false;try{Jt::Importer::read(path,options);}catch(const Jt::Cancelled&){cancelled=true;}check(cancelled,"Warehouse cancellation failed");
    std::cout<<"Warehouse: 1084 meshes, "<<vertices<<" vertices, "<<triangles<<" triangles; local-coordinate bounds "<<lo[0]<<','<<lo[1]<<','<<lo[2]<<" to "<<hi[0]<<','<<hi[1]<<','<<hi[2]<<"; normals, indices and cancellation passed\n";
}
