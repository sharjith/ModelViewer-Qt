// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original Replace/Modulate expectations from explicit internal RGB/A components.
#include "JtTextureFixture.h"
#include "JtLegacyFixture.h"
#include "Jt/JtImporter.h"
#include <QTemporaryDir>
#include <QImage>
#include <QColor>
#include <cmath>
#include <iostream>
namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
}
void textureReplaceTests(){
    QTemporaryDir dir;check(dir.isValid(),"Cannot create Replace test directory");
    LegacyFixture::Options geometry;geometry.quantizedData=true;geometry.quantizedBits=4;geometry.uvBinding=1;geometry.materialAlpha=.5f;
    geometry.materialDataFlags=0x3990;LegacyFixture::write(dir.filePath("blend.jt"),geometry);
    geometry.materialDataFlags=0x3980;LegacyFixture::write(dir.filePath("opaque.jt"),geometry);
    geometry.colorBinding=1;LegacyFixture::write(dir.filePath("coloured.jt"),geometry);
    for(unsigned family:{9u,10u})for(unsigned minor:family==9?std::vector<unsigned>{0,5}:std::vector<unsigned>{0,3,5,7,8,9,10})
    for(unsigned version:family==9?std::vector<unsigned>{1,2,3}:std::vector<unsigned>{1})for(bool big:{false,true}){
        TextureFixture::Options base;base.big=big;base.coordinate=0;base.version=version;base.legacyType=version==1?2:9;base.baseVersion=family==10&&minor?2:1;base.wrapped=family==10&&minor;base.blendType=3;
        const auto write=[&](TextureFixture::Options o,const QString& file){if(family==9)TextureFixture::write9(dir.filePath("replace.jt"),minor,o,file);else TextureFixture::write(dir.filePath("replace.jt"),minor,o,file);};
        for(unsigned format:{1u,2u,11u,12u})for(bool blend:{false,true}){
            auto o=base;o.pixelFormat=format;const bool alpha=format==2||format==12,bgr=format>=11;o.pixels=QByteArray("");
            for(int i=0;i<4;++i){o.pixels+=QByteArray::fromHex(bgr?"996633":"336699");if(alpha)o.pixels.append(char(64));}
            o.total=o.mipBytes=int(o.pixels.size());write(o,blend?"blend.jt":"opaque.jt");
            const auto result=Jt::Importer::read(dir.filePath("replace.jt"));check(result.scene->mNumMeshes==1,"Replace lost geometry");
            const auto* material=result.scene->mMaterials[result.scene->mMeshes[0]->mMaterialIndex];aiColor3D color;float opacity=0;aiString path,mode;
            check(material->Get(AI_MATKEY_COLOR_DIFFUSE,color)==AI_SUCCESS&&color==aiColor3D(1,1,1),"Replace retained incoming diffuse RGB");
            check(material->Get(AI_MATKEY_OPACITY,opacity)==AI_SUCCESS&&std::abs(opacity-((blend&&!alpha)?.5f:1.f))<1e-6f,"Replace internal RGB/A opacity differs");
            check((material->Get("$mat.gltf.alphaMode",0,0,mode)==AI_SUCCESS&&mode==aiString("BLEND"))==(blend&&alpha),"Replace texture alpha selected wrong pass");
            check(material->GetTexture(aiTextureType_DIFFUSE,0,&path)==AI_SUCCESS,"Replace image association missing");const auto* image=result.scene->GetEmbeddedTexture(path.C_Str());check(image,"Replace image missing");
            const auto pixels=QImage::fromData(reinterpret_cast<const uchar*>(image->pcData),int(image->mWidth));
            check(pixels.pixelColor(0,0)==QColor(51,102,153,(blend&&alpha)?64:255),"Replace changed source colour/order/alpha policy");
        }
        for(int mode:{0,1,4,5,6}){auto o=base;o.blendType=mode;write(o,"blend.jt");const auto result=Jt::Importer::read(dir.filePath("replace.jt"));
            check(result.scene->mNumMeshes==1&&!result.warnings.isEmpty()&&result.scene->mMaterials[result.scene->mMeshes[0]->mMaterialIndex]->GetTextureCount(aiTextureType_DIFFUSE)==0,"Unqualified combiner did not retain geometry with diagnostic");}
        auto o=base;write(o,"coloured.jt");const auto coloured=Jt::Importer::read(dir.filePath("replace.jt"));
        check(coloured.scene->mMeshes[0]->HasVertexColors(0)&&!coloured.warnings.isEmpty()&&coloured.scene->mMaterials[coloured.scene->mMeshes[0]->mMaterialIndex]->GetTextureCount(aiTextureType_DIFFUSE)==0,"Unqualified vertex-colour Replace lost colours or kept active image");
    }
    auto replace=TextureFixture::Options{};replace.coordinate=0;replace.blendType=3;auto modulate=replace;modulate.blendType=2;
    TextureFixture::writePair(dir.filePath("pair.jt"),0,replace,modulate,"blend.jt");const auto pair=Jt::Importer::read(dir.filePath("pair.jt"));
    check(pair.scene->mNumMeshes==2&&pair.scene->mNumTextures==1,"Replace/Modulate sharing lost geometry or duplicated image");
    aiColor3D a,b;pair.scene->mMaterials[pair.scene->mMeshes[0]->mMaterialIndex]->Get(AI_MATKEY_COLOR_DIFFUSE,a);pair.scene->mMaterials[pair.scene->mMeshes[1]->mMaterialIndex]->Get(AI_MATKEY_COLOR_DIFFUSE,b);
    check(a!=b,"Replace/Modulate material variants aliased");
    std::cout<<"Inline RGB/RGBA Replace colours, opacity, endian/family mapping, variants and bounded fallback passed\n";
}
