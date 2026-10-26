// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#include "JtTextureFixture.h"
#include "JtAlphaCubeFixture.h"
#include "JtLegacyFixture.h"
#include "SceneUtils.h"
#include "Jt/JtImporter.h"
#include <JtAttribute_TextureImage.hxx>
#include <JtNode_Partition.hxx>
#include <JtData_Reader.hxx>
#include <JtData_Limits.hxx>
#include <QTemporaryDir>
#include <QDir>
#include <QImage>
#include <assimp/Importer.hpp>
#include <algorithm>
#include <iostream>
namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class F>void reject(F f){try{f();}catch(const Jt::Cancelled&){throw;}catch(const std::exception&){return;}throw std::runtime_error("Invalid texture descriptor accepted");}
struct TextureTestModel:JtData_Model {TextureTestModel(unsigned minor,bool big):JtData_Model(TCollection_ExtendedString()){myMajorVersion=10;myMinorVersion=minor;myIsFileLE=!big;}};
struct Reader:JtData_Reader {
    QByteArray data;size_t pos=0;Reader(const Fixture::Bytes& b,unsigned minor):JtData_Reader(new TextureTestModel(minor,b.big)),data(b.data){}
    Standard_Boolean ReadBytes(void* out,Standard_Size n)override{if(n>size_t(data.size())-pos)return false;if(n)std::memcpy(out,data.constData()+pos,n);pos+=n;return true;}
    Standard_Boolean SkipBytes(Standard_Size n)override{if(n>size_t(data.size())-pos)return false;pos+=n;return true;}
    Standard_Size GetPosition()const override{return pos;}
};
void decode(const Fixture::Bytes& b,unsigned minor,bool inlineImage){
    Reader reader(b,minor);Handle(JtAttribute_TextureImage) texture=new JtAttribute_TextureImage;
    check(texture->Read(reader)&&reader.pos==size_t(b.data.size()),"Texture envelope/truncation mismatch");
    check(texture->Parsed()&&texture->TextureType()==9&&texture->Channel()==0&&texture->CoordinateChannel()==2&&texture->Inline()==inlineImage,"Texture fields shifted");
    check(texture->Images().size()==1&&texture->Environment()[6]==2&&texture->Transform()[15]==1,"Texture environment changed");
    const auto& image=texture->Images()[0];
    if(inlineImage)check(image.width==2&&image.height==2&&image.format==1&&image.totalBytes==12&&image.mipBytes==std::vector<Jt_I32>{12},"Inline texture descriptor changed");
    else check(image.externalName==TCollection_ExtendedString("original_texture.png"),"External texture name changed");
}
void checkScene(const QString& path){
    const auto result=Jt::Importer::read(path);const auto& scene=*result.scene;
    check(scene.mNumMeshes==2&&scene.mMeshes[0]->mNumFaces==12&&scene.mMeshes[1]->mNumFaces==12,"Texture descriptors changed geometry");
    aiMetadata textures;check(scene.mRootNode->mMetaData&&scene.mRootNode->mMetaData->Get("JTTextures",textures)&&textures.mNumProperties==1,"Texture metadata missing");
    aiMetadata descriptor;int32_t channel=-1;
    check(textures.Get(0u,descriptor)&&descriptor.Get("CoordinateChannel",channel)&&channel==2,"Texture metadata shifted");
    check(std::any_of(result.warnings.begin(),result.warnings.end(),[](const QString& s){return s.contains("JT texture not applied:");}),"Texture fallback was silent");
}
void mappedImages(const QString& folder){
    QDir dir(folder);LegacyFixture::Options geometry;geometry.quantizedData=true;geometry.quantizedBits=4;geometry.uvBinding=1;
    LegacyFixture::write(dir.filePath("uv_geometry.jt"),geometry);
    const auto original=Jt::Importer::read(dir.filePath("uv_geometry.jt"));
    QImage expected(2,2,QImage::Format_RGBA8888);
    expected.setPixelColor(0,0,Qt::blue);expected.setPixelColor(1,0,Qt::white);
    expected.setPixelColor(0,1,Qt::red);expected.setPixelColor(1,1,Qt::green);
    check(expected.save(dir.filePath("original_texture.png")),"Cannot write original test image");
    for(unsigned minor:{0u,3u,5u,7u,8u,9u,10u})for(bool big:{false,true})for(bool inlined:{false,true}){
        TextureFixture::Options o;o.coordinate=0;o.big=big;o.inlineImage=inlined;o.baseVersion=minor?2:1;o.wrapped=minor!=0;
        const auto path=dir.filePath("mapped.jt");TextureFixture::write(path,minor,o,"uv_geometry.jt");
        const auto result=Jt::Importer::read(path);const auto& scene=*result.scene;
        check(scene.mNumTextures==1&&scene.mTextures[0]->mHeight==0,"Embedded PNG missing or duplicated");
        const auto* image=scene.mTextures[0];const auto decoded=QImage::fromData(reinterpret_cast<const uchar*>(image->pcData),int(image->mWidth));
        check(decoded.convertToFormat(QImage::Format_RGBA8888)==expected,"Inline/external image pixels or row orientation differ");
        check(scene.mNumMeshes==1&&scene.mMeshes[0]->HasTextureCoords(0),"Textured mesh or UV0 missing");
        const auto& mesh=*scene.mMeshes[0];const auto& baseline=*original.scene->mMeshes[0];
        check(mesh.mNumVertices==baseline.mNumVertices&&mesh.mNumFaces==baseline.mNumFaces,"Image mapping changed geometry");
        for(unsigned i=0;i<mesh.mNumVertices;++i)check(mesh.mTextureCoords[0][i]==baseline.mTextureCoords[0][i],"Image mapping rewrote stored UVs");
        const auto* material=scene.mMaterials[mesh.mMaterialIndex];aiString resource;int uv=99;
        check(material->GetTexture(aiTextureType_DIFFUSE,0,&resource)==AI_SUCCESS&&std::string(resource.C_Str())=="*0","Image not bound to mesh material");
        const auto uvStatus=material->Get(AI_MATKEY_UVWSRC(aiTextureType_DIFFUSE,0),uv);
        check(uvStatus==AI_SUCCESS&&uv==0,("Image selected wrong UV set (minor "+std::to_string(minor)+", big "+std::to_string(big)+", inline "+std::to_string(inlined)
            +", status "+std::to_string(int(uvStatus))+", uv "+std::to_string(uv)+")").c_str());
        aiUVTransform transform;
        check(material->Get(AI_MATKEY_UVTRANSFORM(aiTextureType_DIFFUSE,0),transform)!=AI_SUCCESS
            ||(transform.mScaling==aiVector2D(1,1)&&transform.mTranslation==aiVector2D(0,0)&&transform.mRotation==0),
            "Identity JT mapping acquired an extra UV reflection");
        std::unique_ptr<aiScene> copy(SceneUtils::deepCopyScene(&scene));
        check(copy&&copy->mNumTextures==1&&copy->mTextures[0]->mWidth==image->mWidth
            &&std::memcmp(copy->mTextures[0]->pcData,image->pcData,image->mWidth)==0,"Scene copy changed compressed texture bytes");
    }
    check(QFile::remove(dir.filePath("original_texture.png")),"Cannot remove external test image");
    TextureFixture::Options missing;missing.coordinate=0;missing.inlineImage=false;
    TextureFixture::write(dir.filePath("missing.jt"),0,missing,"uv_geometry.jt");
    const auto fallback=Jt::Importer::read(dir.filePath("missing.jt"));
    check(fallback.scene->mNumMeshes==1&&fallback.scene->mNumTextures==0&&!fallback.warnings.isEmpty(),"Missing image did not preserve geometry with warning");
}
void assimpUVConvention(){
    // Original glTF triangle: verify the convention at the shared renderer boundary.
    // Assimp already converts glTF's top-left V into JT/OpenGL's bottom-left V.
    Fixture::Bytes payload;
    for(float x:{0.f,0.f,0.f,1.f,0.f,0.f,0.f,1.f,0.f})payload.f32(x);
    for(float x:{0.f,0.f,1.f,0.f,0.f,1.f})payload.f32(x);
    const QByteArray json=QByteArray(R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":60,"uri":"data:application/octet-stream;base64,)")
        +payload.data.toBase64()+R"("}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":24}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC2"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1}}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
    Assimp::Importer importer;const auto* scene=importer.ReadFileFromMemory(json.constData(),size_t(json.size()),0,"gltf");
    check(scene&&scene->mNumMeshes==1&&scene->mMeshes[0]->HasTextureCoords(0),"Original glTF UV control failed to import");
    const auto* uv=scene->mMeshes[0]->mTextureCoords[0];
    check(uv[0]==aiVector3D(0,1,0)&&uv[1]==aiVector3D(1,1,0)&&uv[2]==aiVector3D(0,0,0),"Shared Assimp/JT UV convention changed");
}
}
void textureTests(){
    QTemporaryDir temp;check(temp.isValid(),"Cannot create texture test directory");
    AlphaCubeFixture::write(temp.filePath("geometry.jt"),2,false,0.5f,0x3980);
    for(unsigned minor:{0u,3u,5u,7u,8u,9u,10u})for(bool big:{false,true})for(bool inlineImage:{false,true}){
        TextureFixture::Options o;o.big=big;o.inlineImage=inlineImage;
        for(unsigned base:{1u,2u}){o.baseVersion=base;auto bytes=TextureFixture::body(o);decode(bytes,minor,inlineImage);
            for(qsizetype n=0;n<bytes.data.size();++n){auto truncated=bytes;truncated.data.truncate(n);reject([&]{decode(truncated,minor,inlineImage);});}}
        for(bool wrapped:{false,true}){o.baseVersion=wrapped?2:1;o.wrapped=wrapped;TextureFixture::write(temp.filePath("texture.jt"),minor,o);checkScene(temp.filePath("texture.jt"));}
        for(unsigned damage=0;damage<6;++damage){auto bad=o;bad.inlineImage=true;
            if(damage==0)bad.version=2;if(damage==1)bad.storage=2;if(damage==2)bad.width=0;
            if(damage==3)bad.total=11;if(damage==4)bad.total=13;if(damage==5)bad.mipBytes=-1;
            reject([&]{decode(TextureFixture::body(bad),minor,true);});}
    }
    auto data=TextureFixture::body();
    {JtData_Limits::Scope scope({},1);reject([&]{decode(data,0,true);});}
    {bool stopped=false;JtData_Limits::Scope scope([]{throw Jt::Cancelled();});try{decode(data,0,true);}catch(const Jt::Cancelled&){stopped=true;}check(stopped,"Texture parser ignored cancellation");}
    mappedImages(temp.path());
    assimpUVConvention();
    std::cout<<"Texture descriptor fields, modern/endian/wrapped containers, metadata, bounds and cancellation passed\n";
}
void textureFixtureFiles(const QString& folder){
    check(QDir().mkpath(folder),"Cannot create texture fixture directory");QDir dir(folder);
    AlphaCubeFixture::write(dir.filePath("geometry.jt"),2,false,0.5f,0x3980);
    for(unsigned minor:{0u,3u,5u,7u,8u,9u,10u})for(bool big:{false,true})for(bool inlineImage:{false,true}){
        TextureFixture::Options o;o.big=big;o.inlineImage=inlineImage;o.baseVersion=minor?2:1;o.wrapped=minor!=0;
        const auto name=QString("texture_10_%1_%2_%3.jt").arg(minor).arg(big?"be":"le").arg(inlineImage?"inline":"external");
        TextureFixture::write(dir.filePath(name),minor,o);checkScene(dir.filePath(name));
    }
}
