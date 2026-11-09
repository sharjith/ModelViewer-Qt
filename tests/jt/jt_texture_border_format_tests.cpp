// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original component bytes and independently listed canonical colours.
#include "JtTextureFixture.h"
#include "JtLegacyFixture.h"
#include "Jt/JtImporter.h"
#include <QImage>
#include <QColor>
#include <QTemporaryDir>
#include <iostream>
namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Format {unsigned code;const char* ring;const char* inner;QColor ringColor,innerColor;};
QImage imageOf(const Jt::ImportResult& result){check(result.scene->mNumMeshes==1,"Border format changed mesh count");aiString path;
    const auto* material=result.scene->mMaterials[result.scene->mMeshes[0]->mMaterialIndex];
    check(material->GetTexture(aiTextureType_DIFFUSE,0,&path)==AI_SUCCESS,"Border format image association missing");
    const auto* image=result.scene->GetEmbeddedTexture(path.C_Str());check(image,"Border format image missing");
    return QImage::fromData(reinterpret_cast<const uchar*>(image->pcData),int(image->mWidth)).convertToFormat(QImage::Format_RGBA8888);}
}
void borderFormatTests(){
    const std::vector<Format> formats={
        {1,"336699","ccaa55",{51,102,153,255},{204,170,85,255}},
        {2,"33669940","ccaa55ff",{51,102,153,64},{204,170,85,255}},
        {3,"33","cc",{51,51,51,255},{204,204,204,255}},
        {4,"3340","ccff",{51,51,51,64},{204,204,204,255}},
        {7,"33","cc",{51,0,0,255},{204,0,0,255}},
        {8,"66","aa",{0,102,0,255},{0,170,0,255}},
        {9,"99","55",{0,0,153,255},{0,0,85,255}},
        {10,"40","ff",{255,255,255,64},{255,255,255,255}},
        {11,"996633","55aacc",{51,102,153,255},{204,170,85,255}},
        {12,"99663340","55aaccff",{51,102,153,64},{204,170,85,255}}};
    QTemporaryDir dir;check(dir.isValid(),"Cannot create border-format test directory");
    LegacyFixture::Options geometry;geometry.quantizedData=true;geometry.quantizedBits=4;geometry.uvBinding=1;geometry.materialDataFlags=0x3990;
    LegacyFixture::write(dir.filePath("geometry.jt"),geometry);
    geometry.materialDataFlags=0x3980;LegacyFixture::write(dir.filePath("opaque.jt"),geometry);
    for(unsigned family:{9u,10u})for(unsigned minor:family==9?std::vector<unsigned>{0,5}:std::vector<unsigned>{0,3,5,7,8,9,10})
    for(unsigned version:family==9?std::vector<unsigned>{1,2,3}:std::vector<unsigned>{1})for(bool big:{false,true}){
        TextureFixture::Options base;base.big=big;base.coordinate=0;base.version=version;base.legacyType=version==1?2:9;
        base.baseVersion=family==10&&minor?2:1;base.wrapped=family==10&&minor;base.wrapS=base.wrapT=5;base.alignment=8;
        const auto write=[&](TextureFixture::Options o,bool blend=true){if(family==9)TextureFixture::write9(dir.filePath("format.jt"),minor,o,blend?"geometry.jt":"opaque.jt");else TextureFixture::write(dir.filePath("format.jt"),minor,o,blend?"geometry.jt":"opaque.jt");};
        for(const auto& format:formats)for(unsigned type:{3u,5u}){
            auto o=base;o.borderMode=2;o.imageBorder=1;o.width=o.height=4;o.pixelFormat=format.code;o.pixelType=type;o.pixels=QByteArray("");
            for(int y=3;y>=0;--y){const auto start=o.pixels.size();for(int x=0;x<4;++x){
                const auto bytes=QByteArray::fromHex((x==0||y==0||x==3||y==3)?format.ring:format.inner);
                for(unsigned char value:bytes){o.pixels.append(char(value));if(type==5)o.pixels.append(char(value));}
            }while((o.pixels.size()-start)%o.alignment)o.pixels.append(char(0xea));}
            o.total=o.mipBytes=int(o.pixels.size());write(o);const auto result=Jt::Importer::read(dir.filePath("format.jt"));const auto image=imageOf(result);
            QImage expected(4,4,QImage::Format_RGBA8888);expected.fill(format.ringColor);
            for(int y=1;y<3;++y)for(int x=1;x<3;++x)expected.setPixelColor(x,y,format.innerColor);
            check(image==expected,"Explicit border colour-format conversion differs");
            aiString mode;const auto* material=result.scene->mMaterials[result.scene->mMeshes[0]->mMaterialIndex];
            check((material->Get("$mat.gltf.alphaMode",0,0,mode)==AI_SUCCESS&&mode==aiString("BLEND"))==(format.ringColor.alpha()<255),"Explicit border format selected wrong alpha pass");
        }
        for(unsigned format:{1u,11u}){
            auto o=base;o.borderMode=1;o.pixelFormat=format;o.borderColor={.2f,.4f,.6f,0};o.pixels=QByteArray("");
            for(int y=0;y<2;++y){const auto start=o.pixels.size();for(int x=0;x<2;++x)o.pixels+=QByteArray::fromHex(format==1?"ccaa55":"55aacc");while((o.pixels.size()-start)%8)o.pixels.append(char(0xea));}
            o.total=o.mipBytes=int(o.pixels.size());write(o);const auto result=Jt::Importer::read(dir.filePath("format.jt"));const auto image=imageOf(result);
            check(image.pixelColor(0,0)==QColor(51,102,153,255)&&image.pixelColor(1,1)==QColor(204,170,85,255),"RGB constant border channel/alpha interpretation differs");
            aiString mode;check(!(result.scene->mMaterials[result.scene->mMeshes[0]->mMaterialIndex]->Get("$mat.gltf.alphaMode",0,0,mode)==AI_SUCCESS&&mode==aiString("BLEND")),"RGB border introduced transparency from unused Border A");
        }
        for(const auto& format:formats)if(format.code==3||format.code==4||format.code==10)for(bool blend:{false,true}){
            auto o=base;o.borderMode=1;o.pixelFormat=format.code;o.borderColor={.2f,.4f,.6f,.25f};o.pixels=QByteArray("");
            for(int y=0;y<2;++y){const auto start=o.pixels.size();for(int x=0;x<2;++x)o.pixels+=QByteArray::fromHex(format.inner);while((o.pixels.size()-start)%8)o.pixels.append(char(0xea));}
            o.total=o.mipBytes=int(o.pixels.size());write(o,blend);const auto result=Jt::Importer::read(dir.filePath("format.jt"));const auto image=imageOf(result);
            const bool alpha=blend&&format.code!=3;
            const QColor border=format.code==10?QColor(255,255,255,alpha?64:255):QColor(51,51,51,alpha?64:255);
            check(image.pixelColor(0,0)==border&&image.pixelColor(1,1)==format.innerColor,"Luminance/alpha constant border interpreted wrong internal components");
            aiString mode;check((result.scene->mMaterials[result.scene->mMeshes[0]->mMaterialIndex]->Get("$mat.gltf.alphaMode",0,0,mode)==AI_SUCCESS&&mode==aiString("BLEND"))==alpha,"Luminance/alpha constant border selected wrong pass");
        }
    }
    QImage rgb(2,2,QImage::Format_RGB888);rgb.fill(QColor(204,170,85));check(rgb.save(dir.filePath("original_texture.png")),"Cannot write RGB external source");
    TextureFixture::Options external;external.coordinate=0;external.inlineImage=false;external.borderMode=1;external.wrapS=external.wrapT=5;external.borderColor={.2f,.4f,.6f,0};
    TextureFixture::write(dir.filePath("external.jt"),0,external);const auto loaded=Jt::Importer::read(dir.filePath("external.jt"));
    check(imageOf(loaded).pixelColor(0,0)==QColor(51,102,153,255),"External RGB constant border introduced alpha");
    std::cout<<"Explicit colour border formats, RGB constant-border alpha, endian/family mapping and external RGB passed\n";
}
