// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original unsigned 8/16-bit and float32 texels and independent RGBA expectations.
#include "JtTextureFixture.h"
#include "JtLegacyFixture.h"
#include "Jt/JtImporter.h"
#include <QTemporaryDir>
#include <QImage>
#include <iostream>
#include <cstring>
namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Pixel {unsigned r,g,b,a;};
const Pixel input[6]={{10,20,30,0},{40,50,60,128},{70,80,90,255},{100,110,120,64},{130,140,150,192},{160,170,180,255}};
void author(TextureFixture::Options& o,QImage& expected,bool alpha){
    expected=QImage(3,2,QImage::Format_RGBA8888);o.width=3;o.height=2;o.pixels=QByteArray("");
    for(int y=1;y>=0;--y){const auto start=o.pixels.size();
        for(int x=0;x<3;++x){const auto p=input[3*y+x];unsigned r=0,g=0,b=0,a=255;
            const auto put=[&](unsigned v){
                if(o.pixelType==2){
                    const float component=float(v)/255.f;uint32_t bits;
                    std::memcpy(&bits,&component,sizeof(bits));
                    for(unsigned i=0;i<4;++i)o.pixels.append(char(bits>>(8*(o.big?3-i:i))));
                }else if(o.pixelType==5){
                    // Unequal bytes make byte-order mistakes visible, while the
                    // independent expected byte remains v after nearest rounding.
                    const unsigned word=v==0?0:v==255?65535:v*257+73;
                    o.pixels.append(char(o.big?word>>8:word));o.pixels.append(char(o.big?word:word>>8));
                }else o.pixels.append(char(v));
            };
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
        for(unsigned pixelType:{2u,3u,5u})
        for(unsigned format:{1u,2u,3u,4u,7u,8u,9u,10u,11u,12u})
        for(int alignment:minor==0?std::vector<int>{1,2,4,8}:std::vector<int>{8}){
            TextureFixture::Options o;o.coordinate=0;o.big=big;o.baseVersion=minor?2:1;o.wrapped=minor!=0;o.pixelFormat=format;o.alignment=alignment;o.pixelType=pixelType;
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
    for(unsigned type:{2u,3u,5u})for(int delta:{-1,1}){
        TextureFixture::Options o;o.coordinate=0;o.pixelFormat=4;o.pixelType=type;o.alignment=8;QImage image;author(o,image,true);
        o.pixels.resize(o.pixels.size()+delta);o.total=o.mipBytes=int(o.pixels.size());TextureFixture::write(dir.filePath("bad_rows.jt"),0,o,"blend.jt");
        const auto result=Jt::Importer::read(dir.filePath("bad_rows.jt"));
        check(result.scene->mNumMeshes==1&&!result.warnings.isEmpty()&&result.scene->mNumTextures==0,"Invalid pixel row count did not fall back");
    }
    // The same numeric conversion must work behind every accepted v9 texture
    // envelope, including its one-byte shared flag and trailing v3 coordinate.
    for(unsigned minor:{0u,5u})for(unsigned version:{1u,2u,3u})for(bool big:{false,true})for(unsigned format:{1u,4u,12u})for(unsigned type:{2u,5u}){
        TextureFixture::Options o;o.big=big;o.version=version;o.legacyType=version==1?2:9;o.coordinate=0;o.pixelType=type;o.pixelFormat=format;o.alignment=8;
        QImage expected;author(o,expected,true);TextureFixture::write9(dir.filePath("numeric_v9.jt"),minor,o,"blend.jt");
        const auto result=Jt::Importer::read(dir.filePath("numeric_v9.jt"));aiString path;
        check(result.scene->mNumMeshes==1&&result.scene->mMaterials[result.scene->mMeshes[0]->mMaterialIndex]->GetTexture(aiTextureType_DIFFUSE,0,&path)==AI_SUCCESS,"V9 numeric image lost geometry/association");
        const auto* image=result.scene->GetEmbeddedTexture(path.C_Str());
        check(image&&QImage::fromData(reinterpret_cast<const uchar*>(image->pcData),int(image->mWidth)).convertToFormat(QImage::Format_RGBA8888)==expected,"V9 numeric pixel/endian conversion differs");
    }
    // Hand-computed UNORM16 rounding boundaries, not just expanded 8-bit values.
    const unsigned words[18]={0,1,128,129,256,257,258,32767,32768,65406,65407,65534,65535,0x1234,0xabcd,0x0100,0x8000,0xff00};
    const unsigned values[18]={0,0,0,1,1,1,1,127,128,254,255,255,255,18,171,1,128,254};
    for(bool big:{false,true}){
        TextureFixture::Options o;o.big=big;o.coordinate=0;o.pixelType=5;o.width=3;o.height=2;o.alignment=8;o.pixels=QByteArray("");
        QImage expected(3,2,QImage::Format_RGBA8888);
        for(int y=1;y>=0;--y){const auto start=o.pixels.size();
            for(int x=0;x<3;++x){const auto index=9*y+3*x;
                for(int c=0;c<3;++c){const auto word=words[index+c];o.pixels.append(char(big?word>>8:word));o.pixels.append(char(big?word:word>>8));}
                expected.setPixelColor(x,y,QColor(int(values[index]),int(values[index+1]),int(values[index+2])));}
            while((o.pixels.size()-start)%8)o.pixels.append(char(0xea));
        }
        o.total=o.mipBytes=int(o.pixels.size());TextureFixture::write(dir.filePath("u16_boundaries.jt"),0,o,"opaque.jt");
        const auto result=Jt::Importer::read(dir.filePath("u16_boundaries.jt"));aiString path;
        check(result.scene->mMaterials[result.scene->mMeshes[0]->mMaterialIndex]->GetTexture(aiTextureType_DIFFUSE,0,&path)==AI_SUCCESS,"Uint16 boundary texture missing");
        const auto* image=result.scene->GetEmbeddedTexture(path.C_Str());
        check(image&&QImage::fromData(reinterpret_cast<const uchar*>(image->pcData),int(image->mWidth)).convertToFormat(QImage::Format_RGBA8888)==expected,"Uint16 rounding boundaries differ");
    }
    // Explicit IEEE bit patterns cover clamp limits, signed zero, subnormal,
    // dyadic fractions and the two float32 neighbours around 0.5.
    const uint32_t floatBits[18]={0xbf800000,0,0x3f000000,0x3f800000,0x40000000,0x3e800000,
        0x3f400000,0x3e000000,0x3f600000,0x80000000,1,0x7f7fffff,0xff7fffff,0x3f7fffff,0x3f000001,0x3effffff,0x3d800000,0x3f700000};
    const unsigned floatExpected[18]={0,0,128,255,255,64,191,32,223,0,0,255,0,255,128,127,16,239};
    for(bool big:{false,true}){
        TextureFixture::Options o;o.big=big;o.coordinate=0;o.pixelType=2;o.width=3;o.height=2;o.alignment=8;o.pixels=QByteArray("");
        QImage expected(3,2,QImage::Format_RGBA8888);
        for(int y=1;y>=0;--y){const auto start=o.pixels.size();
            for(int x=0;x<3;++x){const auto index=9*y+3*x;
                for(int c=0;c<3;++c)for(unsigned b=0;b<4;++b)o.pixels.append(char(floatBits[index+c]>>(8*(big?3-b:b))));
                expected.setPixelColor(x,y,QColor(int(floatExpected[index]),int(floatExpected[index+1]),int(floatExpected[index+2])));}
            while((o.pixels.size()-start)%8)o.pixels.append(char(0xea));}
        o.total=o.mipBytes=int(o.pixels.size());TextureFixture::write(dir.filePath("float_boundaries.jt"),0,o,"opaque.jt");
        const auto result=Jt::Importer::read(dir.filePath("float_boundaries.jt"));aiString path;
        check(result.scene->mMaterials[result.scene->mMeshes[0]->mMaterialIndex]->GetTexture(aiTextureType_DIFFUSE,0,&path)==AI_SUCCESS,"Float32 boundary image missing");
        const auto* image=result.scene->GetEmbeddedTexture(path.C_Str());
        check(image&&QImage::fromData(reinterpret_cast<const uchar*>(image->pcData),int(image->mWidth)).convertToFormat(QImage::Format_RGBA8888)==expected,"Float32 clamp/rounding/endian conversion differs");
        for(uint32_t invalid:{0x7fc00000u,0x7f800001u,0x7f800000u,0xff800000u})for(bool alpha:{false,true})for(unsigned component:{0u,4u}){
            o.pixelFormat=4;author(o,expected,alpha);
            // Corrupt alpha as well as colour: disabled blending must not mask
            // invalid input components before validation.
            for(unsigned b=0;b<4;++b)o.pixels[qsizetype(component+b)]=char(invalid>>(8*(big?3-b:b)));
            TextureFixture::write(dir.filePath("float_invalid.jt"),0,o,alpha?"blend.jt":"opaque.jt");
            const auto bad=Jt::Importer::read(dir.filePath("float_invalid.jt"));
            check(bad.scene->mNumMeshes==1&&bad.scene->mNumTextures==0&&!bad.warnings.isEmpty(),"Nonfinite float image did not retain geometry with diagnostic");
        }
    }
    for(unsigned type:{0u,1u,4u,6u,7u,8u,9u}){
        TextureFixture::Options o;o.coordinate=0;o.pixelType=type;TextureFixture::write(dir.filePath("other_type.jt"),0,o,"opaque.jt");
        const auto result=Jt::Importer::read(dir.filePath("other_type.jt"));
        check(result.scene->mNumMeshes==1&&result.scene->mNumTextures==0&&!result.warnings.isEmpty(),"Unsupported numeric type lost fallback");
    }
    std::cout<<"Inline unsigned 8/16-bit and float32 formats, rounding, alpha policy, alignment, byte orders and fallback passed\n";
}
