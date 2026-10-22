// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original bright/transparent border ring around four independently known texels.
#include "JtTextureFixture.h"
#include "JtLegacyFixture.h"
#include "Jt/JtImporter.h"
#include <QImage>
#include <QColor>
#include <QTemporaryDir>
#include <cmath>
#include <cstring>
#include <iostream>
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
QImage expected(){QImage image(2,2,QImage::Format_RGBA8888);image.setPixelColor(0,0,Qt::blue);image.setPixelColor(1,0,Qt::white);image.setPixelColor(0,1,Qt::red);image.setPixelColor(1,1,Qt::green);return image;}
void pixels(TextureFixture::Options& o,bool distinctive=false){
    const auto interior=expected();o.width=o.height=o.imageBorder?4:2;o.pixelFormat=2;o.pixels=QByteArray("");
    for(int y=o.height-1;y>=0;--y){const auto start=o.pixels.size();
        for(int x=0;x<o.width;++x){
            const bool ring=o.imageBorder&&(x==0||y==0||x==o.width-1||y==o.height-1);
            const auto color=ring?(distinctive?QColor(40*x,60*y,200,64):QColor(255,0,255,0)):interior.pixelColor(x-o.imageBorder,y-o.imageBorder);
            for(int value:{color.red(),color.green(),color.blue(),color.alpha()}){
                if(o.pixelType==2){const float f=float(value)/255.f;uint32_t bits;std::memcpy(&bits,&f,sizeof(bits));for(unsigned b=0;b<4;++b)o.pixels.append(char(bits>>(8*(o.big?3-b:b))));}
                else if(o.pixelType==5){const auto word=unsigned(value)*257;for(unsigned b=0;b<2;++b)o.pixels.append(char(word>>(8*(o.big?1-b:b))));}
                else o.pixels.append(char(value));
            }
        }
        while((o.pixels.size()-start)%o.alignment)o.pixels.append(char(0xea));
    }
    o.total=o.mipBytes=int(o.pixels.size());
}
}

void explicitBorderTests(){
    QTemporaryDir dir;check(dir.isValid(),"Cannot create sampled-ring test directory");
    LegacyFixture::Options geometry;geometry.quantizedData=true;geometry.quantizedBits=4;geometry.uvBinding=1;
    geometry.materialDataFlags=0x3990;LegacyFixture::write(dir.filePath("blend.jt"),geometry);
    geometry.materialDataFlags=0x3980;LegacyFixture::write(dir.filePath("opaque.jt"),geometry);
    const auto baseline=Jt::Importer::read(dir.filePath("opaque.jt"));
    const auto imageOf=[](const Jt::ImportResult& result,const aiMesh& mesh){aiString path;
        check(result.scene->mMaterials[mesh.mMaterialIndex]->GetTexture(aiTextureType_DIFFUSE,0,&path)==AI_SUCCESS,"Sampled ring image association missing");
        const auto* image=result.scene->GetEmbeddedTexture(path.C_Str());check(image,"Sampled ring image missing");
        return QImage::fromData(reinterpret_cast<const uchar*>(image->pcData),int(image->mWidth)).convertToFormat(QImage::Format_RGBA8888);};
    for(unsigned family:{9u,10u})for(unsigned minor:family==9?std::vector<unsigned>{0,5}:std::vector<unsigned>{0,3,5,7,8,9,10})
    for(unsigned version:family==9?std::vector<unsigned>{1,2,3}:std::vector<unsigned>{1})for(bool big:{false,true}){
        TextureFixture::Options base;base.big=big;base.coordinate=0;base.version=version;base.legacyType=version==1?2:9;
        base.baseVersion=family==10&&minor?2:1;base.wrapped=family==10&&minor;base.borderMode=2;base.imageBorder=1;base.alignment=8;
        const auto write=[&](TextureFixture::Options o,bool blend){if(family==9)TextureFixture::write9(dir.filePath("ring.jt"),minor,o,blend?"blend.jt":"opaque.jt");else TextureFixture::write(dir.filePath("ring.jt"),minor,o,blend?"blend.jt":"opaque.jt");};
        for(unsigned axes:{1u,2u,3u})for(bool blend:{false,true})for(unsigned type:{2u,3u,5u})for(int filter:{1,2}){
            auto o=base;const bool s=axes&1,t=axes&2;o.wrapS=s?5:3;o.wrapT=t?5:2;o.minFilter=filter;o.pixelType=type;pixels(o,true);write(o,blend);
            const auto result=Jt::Importer::read(dir.filePath("ring.jt"));check(result.scene->mNumMeshes==1,"Sampled ring changed mesh count");
            const auto& mesh=*result.scene->mMeshes[0];const auto& original=*baseline.scene->mMeshes[0];const auto actual=imageOf(result,mesh);
            QImage full(4,4,QImage::Format_RGBA8888);const auto inner=expected();
            for(int y=0;y<4;++y)for(int x=0;x<4;++x){const bool ring=x==0||y==0||x==3||y==3;
                auto c=ring?QColor(40*x,60*y,200,64):inner.pixelColor(x-1,y-1);if(!blend)c.setAlpha(255);full.setPixelColor(x,y,c);}
            check(actual==full.copy(int(!s),int(!t),s?4:2,t?4:2),"Sampled ring pixels, alpha or mixed-axis crop differ");
            check(mesh.mNumVertices==original.mNumVertices&&mesh.mNumFaces==original.mNumFaces,"Sampled ring changed geometry counts");
            for(unsigned i=0;i<mesh.mNumVertices;++i){const auto old=original.mTextureCoords[0][i],uv=mesh.mTextureCoords[0][i];
                check(mesh.mVertices[i]==original.mVertices[i]&&std::abs(uv.x-(s?old.x*.5f+.25f:old.x))<1e-6f&&std::abs(uv.y-(t?old.y*.5f+.25f:old.y))<1e-6f,"Sampled ring interior-relative UV mapping differs");}
            aiString alpha;const auto* material=result.scene->mMaterials[mesh.mMaterialIndex];
            check((material->Get("$mat.gltf.alphaMode",0,0,alpha)==AI_SUCCESS&&alpha==aiString("BLEND"))==blend,"Sampled ring alpha selected wrong pass");
        }
        auto o=base;o.wrapS=o.wrapT=5;o.pixelType=2;pixels(o,true);const uint32_t nan=0x7fc00000;
        for(unsigned b=0;b<4;++b)o.pixels[qsizetype(b)]=char(nan>>(8*(big?3-b:b)));write(o,false);
        const auto invalid=Jt::Importer::read(dir.filePath("ring.jt"));check(invalid.scene->mNumMeshes==1&&invalid.scene->mNumTextures==0&&!invalid.warnings.isEmpty(),"Nonfinite sampled ring escaped validation");
    }
    // Shared image attribute inherits two different wrap-axis choices. The
    // cropped images must remain distinct even though their source is identical.
    using namespace Fixture;Bytes graph(false);element(graph,type("10dd103e"),1,partition(false,{}, {2,3}));
    element(graph,type("10dd103e"),2,partition(false,{}, {4},{21}));element(graph,type("10dd103e"),3,partition(false,{}, {5},{22}));
    element(graph,type("10dd103e"),4,partition(false,"opaque.jt",{}, {20}));element(graph,type("10dd103e"),5,partition(false,"opaque.jt",{}, {20}));
    TextureFixture::Options shared;shared.coordinate=0;shared.borderMode=2;shared.imageBorder=1;shared.inhibit=8;pixels(shared,true);
    auto first=shared,second=shared;first.inhibit=second.inhibit=0;first.wrapS=5;first.wrapT=2;second.wrapS=2;second.wrapT=5;
    element(graph,type("10dd1073"),20,TextureFixture::body(shared),3);element(graph,type("10dd1073"),21,TextureFixture::body(first),3);element(graph,type("10dd1073"),22,TextureFixture::body(second),3);
    end(graph);end(graph);graph.u16(1);graph.u32(0);container(dir.filePath("pair.jt"),graph,0);
    const auto pair=Jt::Importer::read(dir.filePath("pair.jt"));check(pair.scene->mNumMeshes==2&&pair.scene->mNumTextures==2,"Shared ring wrap-axis appearances aliased");
    const auto a=imageOf(pair,*pair.scene->mMeshes[0]),b=imageOf(pair,*pair.scene->mMeshes[1]);
    check((a.size()==QSize(4,2)&&b.size()==QSize(2,4))||(a.size()==QSize(2,4)&&b.size()==QSize(4,2)),"Shared ring crop cache ignored inherited axes");
    std::cout<<"Sampled explicit border pixels, mixed wraps, UVs, alpha, numeric/endian/family mapping and shared cache passed\n";
}
void textureBorderTests(){
    QTemporaryDir dir;check(dir.isValid(),"Cannot create border test directory");
    LegacyFixture::Options geometry;geometry.quantizedData=true;geometry.quantizedBits=4;geometry.uvBinding=1;geometry.materialDataFlags=0x3990;
    LegacyFixture::write(dir.filePath("geometry.jt"),geometry);const auto baseline=Jt::Importer::read(dir.filePath("geometry.jt"));
    for(unsigned family:{9u,10u})for(unsigned minor:family==9?std::vector<unsigned>{0,5}:std::vector<unsigned>{0,3,5,7,8,9,10})
    for(unsigned version:family==9?std::vector<unsigned>{1,2,3}:std::vector<unsigned>{1})for(bool big:{false,true}){
        const auto write=[&](TextureFixture::Options o){if(family==9)TextureFixture::write9(dir.filePath("border.jt"),minor,o);else TextureFixture::write(dir.filePath("border.jt"),minor,o);};
        TextureFixture::Options base;base.big=big;base.coordinate=0;base.version=version;base.legacyType=version==1?2:9;base.baseVersion=family==10&&minor?2:1;base.wrapped=family==10&&minor;base.alignment=8;
        for(int mode:{0,1,2})for(int wrap:{2,3,4})for(unsigned type:{2u,3u,5u}){
            auto o=base;o.borderMode=mode;o.imageBorder=mode==2?1:0;o.wrapS=o.wrapT=wrap;o.pixelType=type;pixels(o);write(o);
            const auto result=Jt::Importer::read(dir.filePath("border.jt"));
            check(result.scene->mNumMeshes==1&&result.scene->mNumTextures==1,"Border mode lost geometry/image");
            const auto& mesh=*result.scene->mMeshes[0];const auto& original=*baseline.scene->mMeshes[0];aiString path;
            const auto* material=result.scene->mMaterials[mesh.mMaterialIndex];
            check(material->GetTexture(aiTextureType_DIFFUSE,0,&path)==AI_SUCCESS,"Border texture association missing");
            const auto* image=result.scene->GetEmbeddedTexture(path.C_Str());
            check(image&&QImage::fromData(reinterpret_cast<const uchar*>(image->pcData),int(image->mWidth)).convertToFormat(QImage::Format_RGBA8888)==expected(),"Ignored ring leaked into texture or source orientation changed");
            aiString alpha;check(!(material->Get("$mat.gltf.alphaMode",0,0,alpha)==AI_SUCCESS&&alpha==aiString("BLEND")),"Unsampled transparent border enabled blending");
            check(mesh.mNumVertices==original.mNumVertices&&mesh.mNumFaces==original.mNumFaces,"Border mapping changed geometry counts");
            for(unsigned i=0;i<mesh.mNumVertices;++i)check(mesh.mVertices[i]==original.mVertices[i]&&mesh.mTextureCoords[0][i]==original.mTextureCoords[0][i],"Border mapping changed positions/UVs");
        }
        for(unsigned damage=0;damage<6;++damage){auto o=base;o.borderMode=2;o.imageBorder=1;pixels(o);
            if(damage==0)o.wrapS=1;if(damage==1){o.wrapT=5;o.minFilter=3;}if(damage==2)o.imageBorder=0;if(damage==3)o.borderMode=0;
            if(damage==4)o.width=2;if(damage==5)o.inlineImage=false;write(o);
            const auto result=Jt::Importer::read(dir.filePath("border.jt"));
            check(result.scene->mNumMeshes==1&&result.scene->mNumTextures==0&&!result.warnings.isEmpty(),"Unsupported/inconsistent border did not retain geometry with diagnostic");
        }
        auto invalid=base;invalid.borderMode=2;invalid.imageBorder=1;invalid.pixelType=2;pixels(invalid);
        const uint32_t nan=0x7fc00000;for(unsigned b=0;b<4;++b)invalid.pixels[qsizetype(b)]=char(nan>>(8*(big?3-b:b)));write(invalid);
        const auto result=Jt::Importer::read(dir.filePath("border.jt"));
        check(result.scene->mNumMeshes==1&&result.scene->mNumTextures==0&&!result.warnings.isEmpty(),"Nonfinite unsampled border escaped input validation");
    }
    std::cout<<"Unsampled image borders, constant-border modes, alpha, byte orders and family fallback passed\n";
}
