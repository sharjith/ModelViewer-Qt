// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original unsigned-byte texels and independent RGBA expectations.
#include "JtTextureFixture.h"
#include "JtLegacyFixture.h"
#include "Jt/JtImporter.h"
#include <QTemporaryDir>
#include <QImage>
#include <iostream>
namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Pixel {unsigned r,g,b,a;};
const Pixel input[6]={{10,20,30,0},{40,50,60,128},{70,80,90,255},{100,110,120,64},{130,140,150,192},{160,170,180,255}};
void author(TextureFixture::Options& o,QImage& expected,bool alpha){
    expected=QImage(3,2,QImage::Format_RGBA8888);o.width=3;o.height=2;o.pixels=QByteArray("");
    for(int y=1;y>=0;--y){const auto start=o.pixels.size();
        for(int x=0;x<3;++x){const auto p=input[3*y+x];unsigned r=0,g=0,b=0,a=255;
            const auto put=[&](unsigned v){o.pixels.append(char(v));};
            switch(o.pixelFormat){
            case 1:put(p.r);put(p.g);put(p.b);r=p.r;g=p.g;b=p.b;break;
            case 2:put(p.r);put(p.g);put(p.b);put(p.a);r=p.r;g=p.g;b=p.b;a=p.a;break;
            case 3:put(p.r);r=g=b=p.r;break;
            case 4:put(p.r);put(p.a);r=g=b=p.r;a=p.a;break;
            case 7:put(p.r);r=p.r;break;
            case 8:put(p.g);g=p.g;break;
            case 9:put(p.b);b=p.b;break;
            case 10:put(p.a);r=g=b=255;a=p.a;break;
            case 11:put(p.b);put(p.g);put(p.r);r=p.r;g=p.g;b=p.b;break;
            case 12:put(p.b);put(p.g);put(p.r);put(p.a);r=p.r;g=p.g;b=p.b;a=p.a;break;
            }
            expected.setPixelColor(x,y,QColor(int(r),int(g),int(b),int(alpha?a:255)));
        }
        while((o.pixels.size()-start)%o.alignment)o.pixels.append(char(0xea));
    }
    o.total=o.mipBytes=int(o.pixels.size());
}
}
void texturePixelTests(){
    QTemporaryDir dir;check(dir.isValid(),"Cannot create pixel test directory");
    LegacyFixture::Options geometry;geometry.quantizedData=true;geometry.quantizedBits=4;geometry.uvBinding=1;geometry.materialDataFlags=0x3990;
    LegacyFixture::write(dir.filePath("blend.jt"),geometry);geometry.materialDataFlags=0x3980;LegacyFixture::write(dir.filePath("opaque.jt"),geometry);
    for(unsigned minor:{0u,3u,5u,7u,8u,9u,10u})for(bool big:{false,true})for(bool alpha:{false,true})
        for(unsigned format:{1u,2u,3u,4u,7u,8u,9u,10u,11u,12u})
        for(int alignment:minor==0?std::vector<int>{1,2,4,8}:std::vector<int>{8}){
            TextureFixture::Options o;o.coordinate=0;o.big=big;o.baseVersion=minor?2:1;o.wrapped=minor!=0;o.pixelFormat=format;o.alignment=alignment;
            QImage expected;author(o,expected,alpha);TextureFixture::write(dir.filePath("pixels.jt"),minor,o,alpha?"blend.jt":"opaque.jt");
            const auto result=Jt::Importer::read(dir.filePath("pixels.jt"));check(result.scene->mNumMeshes==1,"Pixel format lost geometry");
            const auto* material=result.scene->mMaterials[result.scene->mMeshes[0]->mMaterialIndex];aiString path;
            check(material->GetTexture(aiTextureType_DIFFUSE,0,&path)==AI_SUCCESS,"Pixel format lost texture association");
            const auto* image=result.scene->GetEmbeddedTexture(path.C_Str());check(image&&image->mHeight==0,"Pixel format did not produce an embedded PNG");
            const auto decoded=QImage::fromData(reinterpret_cast<const uchar*>(image->pcData),int(image->mWidth));
            check(decoded.convertToFormat(QImage::Format_RGBA8888)==expected,"Pixel components, alpha, padding or row origin changed");
            const bool hasAlpha=alpha&&(format==2||format==4||format==10||format==12);
            aiString mode;const bool blend=material->Get("$mat.gltf.alphaMode",0,0,mode)==AI_SUCCESS&&mode==aiString("BLEND");
            check(blend==hasAlpha,"Image alpha did not select the expected render pass");
        }
    for(unsigned format:{0u,5u,6u,13u}){
        TextureFixture::Options o;o.coordinate=0;o.pixelFormat=format;TextureFixture::write(dir.filePath("unsupported.jt"),0,o,"opaque.jt");
        const auto result=Jt::Importer::read(dir.filePath("unsupported.jt"));
        check(result.scene->mNumMeshes==1&&!result.warnings.isEmpty(),"Non-colour pixel format lost geometry or diagnostic");
        check(result.scene->mMaterials[result.scene->mMeshes[0]->mMaterialIndex]->GetTextureCount(aiTextureType_DIFFUSE)==0,"Non-colour pixels were misinterpreted as colour");
    }
    // Valid descriptors with too few/many base-row bytes must not overread or
    // silently treat padding as pixels. Keep descriptor aggregate counts valid.
    for(int delta:{-1,1}){
        TextureFixture::Options o;o.coordinate=0;o.pixelFormat=4;o.alignment=8;QImage image;author(o,image,true);
        o.pixels.resize(o.pixels.size()+delta);o.total=o.mipBytes=int(o.pixels.size());TextureFixture::write(dir.filePath("bad_rows.jt"),0,o,"blend.jt");
        const auto result=Jt::Importer::read(dir.filePath("bad_rows.jt"));
        check(result.scene->mNumMeshes==1&&!result.warnings.isEmpty()&&result.scene->mNumTextures==0,"Invalid pixel row count did not fall back");
    }
    std::cout<<"Inline colour formats, RGBA alpha policy, row alignment, byte orders and image fallback passed\n";
}
