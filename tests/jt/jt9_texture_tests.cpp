// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original transitional envelopes, basic texture records and pixel expectations.
#include "JtTextureFixture.h"
#include "JtLegacyFixture.h"
#include "Jt/JtImporter.h"
#include <JtAttribute_TextureImage.hxx>
#include <JtData_Reader.hxx>
#include <JtData_Limits.hxx>
#include <QImage>
#include <QTemporaryDir>
#include <iostream>
#include <cmath>
namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class F>void reject(F action){try{action();}catch(const Jt::Cancelled&){throw;}catch(const std::exception&){return;}throw std::runtime_error("Invalid v9 texture accepted");}
struct Texture9Model:JtData_Model {Texture9Model(bool big,unsigned minor):JtData_Model(TCollection_ExtendedString()){myMajorVersion=9;myMinorVersion=minor;myIsFileLE=!big;}};
struct Reader:JtData_Reader {
    QByteArray data;size_t pos=0;Reader(const Fixture::Bytes& b,unsigned minor):JtData_Reader(new Texture9Model(b.big,minor)),data(b.data){}
    Standard_Boolean ReadBytes(void* out,Standard_Size n)override{if(n>size_t(data.size())-pos)return false;if(n)std::memcpy(out,data.constData()+pos,n);pos+=n;return true;}
    Standard_Boolean SkipBytes(Standard_Size n)override{if(n>size_t(data.size())-pos)return false;pos+=n;return true;}
    Standard_Size GetPosition()const override{return pos;}
};
void decode(const Fixture::Bytes& bytes,unsigned minor,bool inlined){
    Reader reader(bytes,minor);Handle(JtAttribute_TextureImage) image=new JtAttribute_TextureImage;
    check(image->Read(reader)&&reader.pos==size_t(bytes.data.size()),"V9 texture envelope shifted/truncated");
    check(image->Parsed()&&image->Legacy9()&&image->LocalVersion()==1&&image->TextureType()==2&&image->CoordinateChannel()==0&&image->Channel()==0,"V9 basic texture fields changed");
    check(image->Inline()==inlined&&image->Images().size()==1,"V9 image storage changed");
    if(inlined)check(image->Images()[0].shared==1&&image->Images()[0].mipBytes==std::vector<Jt_I32>{12}&&image->Images()[0].baseMip.size()==12,"V9 one-byte shared flag/mip envelope shifted");
}
}
void jt9TextureTests(){
    QTemporaryDir dir;check(dir.isValid(),"Cannot create v9 texture test directory");
    LegacyFixture::Options geometry;geometry.quantizedData=true;geometry.quantizedBits=4;geometry.uvBinding=1;LegacyFixture::write(dir.filePath("geometry.jt"),geometry);
    geometry.uvBinding=0;LegacyFixture::write(dir.filePath("no_uv.jt"),geometry);
    const auto baseline=Jt::Importer::read(dir.filePath("geometry.jt"));
    QImage expected(2,2,QImage::Format_RGBA8888);expected.setPixelColor(0,0,Qt::blue);expected.setPixelColor(1,0,Qt::white);expected.setPixelColor(0,1,Qt::red);expected.setPixelColor(1,1,Qt::green);
    check(expected.save(dir.filePath("original_texture.png")),"Cannot write original external test image");
    for(unsigned minor:{0u,5u})for(bool big:{false,true})for(bool inlined:{false,true}){
        TextureFixture::Options o;o.big=big;o.inlineImage=inlined;o.shared=1;o.inhibit=1u<<8;
        const auto bytes=TextureFixture::body9(o);decode(bytes,minor,inlined);
        for(qsizetype n=0;n<bytes.data.size();++n){auto shortData=bytes;shortData.data.truncate(n);reject([&]{decode(shortData,minor,inlined);});}
        for(unsigned test=0;test<3;++test){
            o.transform=TextureFixture::Options{}.transform;o.generation={0,0,0,0};o.planes={};
            if(test==1){o.transform[0]=-1;o.transform[12]=1;}
            if(test==2){o.generation={1,1,0,0};o.planes[0]=1;o.planes[5]=1;}
            TextureFixture::write9(dir.filePath("texture.jt"),minor,o,test==2?"no_uv.jt":"geometry.jt");const auto result=Jt::Importer::read(dir.filePath("texture.jt"));
            check(result.scene->mNumMeshes==1&&result.scene->mNumTextures==1,"V9 image/geometry missing");
            const auto& mesh=*result.scene->mMeshes[0];const auto& source=*baseline.scene->mMeshes[0];const auto* material=result.scene->mMaterials[mesh.mMaterialIndex];aiString resource;
            check(material->GetTexture(aiTextureType_DIFFUSE,0,&resource)==AI_SUCCESS,"Basic v9 type 2 was not mapped to a 2D image");
            const auto* texture=result.scene->GetEmbeddedTexture(resource.C_Str());check(texture&&texture->mHeight==0,"V9 embedded PNG missing");
            check(QImage::fromData(reinterpret_cast<const uchar*>(texture->pcData),int(texture->mWidth)).convertToFormat(QImage::Format_RGBA8888)==expected,"V9 pixels differ from modern mapping");
            check(mesh.mNumVertices==source.mNumVertices&&mesh.mNumFaces==source.mNumFaces,"V9 texture changed geometry counts");
            for(unsigned i=0;i<mesh.mNumVertices;++i){const auto old=source.mTextureCoords[0][i],uv=mesh.mTextureCoords[0][i];
                const float s=test==2?source.mVertices[i].x:test==1?1-old.x:old.x,t=test==2?source.mVertices[i].y:old.y;
                check(std::abs(s-uv.x)<1e-5f&&std::abs(t-uv.y)<1e-5f&&mesh.mVertices[i]==source.mVertices[i],"V9 UV0 default, affine or plane mapping changed");}
            aiMetadata records,descriptor;int32_t type=0,version=0;uint64_t inhibit=0;
            check(result.scene->mRootNode->mMetaData&&result.scene->mRootNode->mMetaData->Get("JTTextures",records)&&records.Get(0u,descriptor)
                &&descriptor.Get("TextureType",type)&&type==2&&descriptor.Get("LocalVersion",version)&&version==1
                &&descriptor.Get("FieldInhibit",inhibit)&&inhibit==256,"V9 source type/version/inhibit metadata was normalized destructively");
        }
        for(unsigned version:{2u,3u}){o.version=version;TextureFixture::write9(dir.filePath("advanced.jt"),minor,o);
            const auto fallback=Jt::Importer::read(dir.filePath("advanced.jt"));check(fallback.scene->mNumMeshes==1&&!fallback.warnings.isEmpty()&&fallback.scene->mNumTextures==0,"Advanced v9 texture did not retain geometry/diagnostic");}
        o.version=1;
        for(unsigned type:{1u,3u,4u,5u,6u}){auto unsupported=o;unsupported.legacyType=type;
            TextureFixture::write9(dir.filePath("other_type.jt"),minor,unsupported);
            const auto fallback=Jt::Importer::read(dir.filePath("other_type.jt"));
            check(fallback.scene->mNumMeshes==1&&!fallback.warnings.isEmpty()&&fallback.scene->mNumTextures==0,"V9 non-2D type acquired modern semantics");}
        for(unsigned damage=0;damage<5;++damage){auto bad=o;if(damage==0)bad.shared=2;if(damage==1)bad.channel=32;if(damage==2)bad.legacyType=9;if(damage==3)bad.version=0;if(damage==4)bad.baseVersion=2;
            if(damage!=0||inlined)reject([&]{decode(TextureFixture::body9(bad),minor,inlined);});}
    }
    TextureFixture::Options o;o.shared=1;auto data=TextureFixture::body9(o);
    {JtData_Limits::Scope budget({},1);reject([&]{decode(data,5,true);});}
    {bool stopped=false;JtData_Limits::Scope cancel([]{throw Jt::Cancelled();});try{decode(data,5,true);}catch(const Jt::Cancelled&){stopped=true;}check(stopped,"V9 texture ignored cancellation");}
    std::cout<<"Basic 9.0/9.5 texture envelopes, pixels, affine/planes, truncation and advanced fallback passed\n";
}
