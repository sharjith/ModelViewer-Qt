// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original Replace/Modulate expectations from explicit internal RGB/A components.
#include "JtTextureFixture.h"
#include "JtTextureStackFixture.h"
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
    geometry.quantizedColourComponents=4;geometry.vertexAlpha=.25f;
    geometry.materialDataFlags=0x3990;LegacyFixture::write(dir.filePath("vertex-alpha.jt"),geometry);
    geometry.materialDataFlags=0x3980;LegacyFixture::write(dir.filePath("vertex-opaque.jt"),geometry);
    QImage external(2,2,QImage::Format_RGBA8888);external.fill(QColor(51,102,153,64));
    check(external.save(dir.filePath("original_texture.png")),"Cannot author original external Replace image");
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
        for(int mode:{6}){auto o=base;o.blendType=mode;write(o,"blend.jt");const auto result=Jt::Importer::read(dir.filePath("replace.jt"));
            check(result.scene->mNumMeshes==1&&!result.warnings.isEmpty()&&result.scene->mMaterials[result.scene->mMeshes[0]->mMaterialIndex]->GetTextureCount(aiTextureType_DIFFUSE)==0,"Unqualified combiner did not retain geometry with diagnostic");}
        for(unsigned format:{1u,2u,11u,12u})for(bool blend:{false,true}){
            auto o=base;o.pixelFormat=format;const bool alpha=format==2||format==12;
            o.pixels=QByteArray();for(int i=0;i<4;++i){o.pixels+=QByteArray::fromHex(format>=11?"996633":"336699");if(alpha)o.pixels.append(char(64));}
            o.total=o.mipBytes=int(o.pixels.size());write(o,blend?"vertex-alpha.jt":"vertex-opaque.jt");
            const auto result=Jt::Importer::read(dir.filePath("replace.jt"));const auto* mesh=result.scene->mMeshes[0];
            const auto* mat=result.scene->mMaterials[mesh->mMaterialIndex];
            check(mesh->HasVertexColors(0)&&mat->GetTextureCount(aiTextureType_DIFFUSE)>0,"Vertex Replace lost image/colour binding");
            for(unsigned i=0;i<mesh->mNumVertices;++i){const auto& c=mesh->mColors[0][i];
                check(c.r==1&&c.g==1&&c.b==1&&std::abs(c.a-((blend&&!alpha)?.25f:1.f))<1e-6f,"Vertex Replace failed RGB discard or alpha selection");}
            float opacity=0;check(mat->Get(AI_MATKEY_OPACITY,opacity)==AI_SUCCESS&&opacity==1,"Vertex alpha was multiplied by material alpha");
            aiString path;check(mat->GetTexture(aiTextureType_DIFFUSE,0,&path)==AI_SUCCESS,"Vertex Replace image missing");
            const auto* encoded=result.scene->GetEmbeddedTexture(path.C_Str());check(encoded,"Vertex Replace embedded image absent");
            const auto image=QImage::fromData(reinterpret_cast<const uchar*>(encoded->pcData),int(encoded->mWidth));
            check(image.pixelColor(0,0)==QColor(51,102,153,blend&&alpha?64:255),"Vertex Replace changed texel components");
        }
        auto o=base;o.coordinate=3;write(o,"coloured.jt");const auto coloured=Jt::Importer::read(dir.filePath("replace.jt"));
        const auto* fallback=coloured.scene->mMeshes[0];
        check(fallback->HasVertexColors(0)&&!coloured.warnings.isEmpty()&&coloured.scene->mMaterials[fallback->mMaterialIndex]->GetTextureCount(aiTextureType_DIFFUSE)==0
            &&fallback->mColors[0][0].r==.25f&&fallback->mColors[0][0].g==.5f,"Missing-UV Replace fallback changed source colours");
        for(int mode:{6}){auto unsupported=base;unsupported.blendType=mode;write(unsupported,"vertex-alpha.jt");
            const auto result=Jt::Importer::read(dir.filePath("replace.jt"));const auto* mesh=result.scene->mMeshes[0];
            check(mesh->HasVertexColors(0)&&mesh->mColors[0][0]==aiColor4D(.25f,.5f,.75f,.25f)
                &&!result.warnings.isEmpty()&&result.scene->mMaterials[mesh->mMaterialIndex]->GetTextureCount(aiTextureType_DIFFUSE)==0,
                "Unqualified vertex combiner did not retain incoming colour/alpha");}
        for(int mode:{1,4,5})for(bool blend:{false,true}){
            auto o=base;o.blendType=mode;o.minFilter=o.magFilter=1;o.pixelFormat=2;
            o.pixels=QByteArray::fromHex("33669940336699403366994033669940");o.total=o.mipBytes=o.pixels.size();
            write(o,blend?"vertex-alpha.jt":"vertex-opaque.jt");const auto result=Jt::Importer::read(dir.filePath("replace.jt"));
            const auto* mesh=result.scene->mMeshes[0];const auto* mat=result.scene->mMaterials[mesh->mMaterialIndex];
            check(mesh->mColors[0][0]==aiColor4D(1,1,1,blend?.25f:1.f),"Constant RGB combiner changed incoming vertex alpha");
            aiString path;check(mat->GetTexture(aiTextureType_DIFFUSE,0,&path)==AI_SUCCESS,"Constant combiner lost texture");
            const auto* t=result.scene->GetEmbeddedTexture(path.C_Str());check(t,"Constant combiner lost pixels");
            const auto pixels=QImage::fromData(reinterpret_cast<const uchar*>(t->pcData),int(t->mWidth));
            QColor expected=mode==1?QColor(122,171,210,255):mode==4?QColor(135,176,189,64):QColor(145,208,255,64);
            if(!blend)expected.setAlpha(255);
            check(pixels.pixelColor(0,0)==expected,"Constant vertex combiner RGB/alpha differs from literal expectation");
            aiColor4D original;check(mat->Get("$mat.jt.sourceDiffuse",0,0,original)==AI_SUCCESS&&original==aiColor4D(0,0,1,.5f),"Vertex composition overwrote source material factors");
        }
        for(unsigned format:{3u,4u,10u}){
            auto o=base;o.pixelFormat=format;o.pixels=QByteArray();
            for(int i=0;i<4;++i)o.pixels+=QByteArray::fromHex(format==3?"33":format==4?"3340":"40");
            o.total=o.mipBytes=o.pixels.size();write(o,"vertex-alpha.jt");
            const auto result=Jt::Importer::read(dir.filePath("replace.jt"));const auto* mesh=result.scene->mMeshes[0];
            const auto expected=format==10?aiColor4D(.25f,.5f,.75f,1):format==3?aiColor4D(1,1,1,.25f):aiColor4D(1,1,1,1);
            check(mesh->mColors[0][0]==expected,"Wider vertex Replace discarded the wrong components");
            aiString path;check(result.scene->mMaterials[mesh->mMaterialIndex]->GetTexture(aiTextureType_DIFFUSE,0,&path)==AI_SUCCESS,"Wider vertex Replace lost texture");
            const auto* encoded=result.scene->GetEmbeddedTexture(path.C_Str());check(encoded,"Wider Replace pixels absent");
            const auto pixels=QImage::fromData(reinterpret_cast<const uchar*>(encoded->pcData),int(encoded->mWidth));
            check(pixels.pixelColor(0,0)==(format==10?QColor(255,255,255,64):QColor(51,51,51,format==3?255:64)),
                "Wider Replace baked preserved vertex components into the image");
        }
        auto externalStage=base;externalStage.inlineImage=false;
        write(externalStage,"vertex-alpha.jt");const auto externalResult=Jt::Importer::read(dir.filePath("replace.jt"));
        const auto* externalMesh=externalResult.scene->mMeshes[0];aiString externalPath;
        check(externalMesh->mColors[0][0]==aiColor4D(1,1,1,1)
            &&externalResult.scene->mMaterials[externalMesh->mMaterialIndex]->GetTexture(aiTextureType_DIFFUSE,0,&externalPath)==AI_SUCCESS,
            "External vertex Replace did not replace incoming components");
        auto filtered=base;filtered.blendType=1;filtered.pixelFormat=2;
        filtered.pixels=QByteArray::fromHex("33669940336699403366994033669940");filtered.total=filtered.mipBytes=filtered.pixels.size();
        write(filtered,"vertex-alpha.jt");const auto nonlinear=Jt::Importer::read(dir.filePath("replace.jt"));
        check(!nonlinear.warnings.isEmpty()&&nonlinear.scene->mMeshes[0]->mColors[0][0]==aiColor4D(.25f,.5f,.75f,.25f)
            &&nonlinear.scene->mMaterials[nonlinear.scene->mMeshes[0]->mMaterialIndex]->GetTextureCount(aiTextureType_DIFFUSE)==0,
            "Filtered nonlinear vertex composition was silently approximated or changed fallback colours");
    }
    auto replace=TextureFixture::Options{};replace.coordinate=0;replace.blendType=3;auto modulate=replace;modulate.blendType=2;
    TextureFixture::writePair(dir.filePath("pair.jt"),0,replace,modulate,"blend.jt");const auto pair=Jt::Importer::read(dir.filePath("pair.jt"));
    check(pair.scene->mNumMeshes==2&&pair.scene->mNumTextures==1,"Replace/Modulate sharing lost geometry or duplicated image");
    aiColor3D a,b;pair.scene->mMaterials[pair.scene->mMeshes[0]->mMaterialIndex]->Get(AI_MATKEY_COLOR_DIFFUSE,a);pair.scene->mMaterials[pair.scene->mMeshes[1]->mMaterialIndex]->Get(AI_MATKEY_COLOR_DIFFUSE,b);
    check(a!=b,"Replace/Modulate material variants aliased");
    TextureFixture::writePair(dir.filePath("vertex-pair.jt"),0,replace,modulate,"vertex-alpha.jt");
    const auto vertexPair=Jt::Importer::read(dir.filePath("vertex-pair.jt"));
    check(vertexPair.scene->mNumMeshes==2&&vertexPair.scene->mMeshes[0]->mColors[0][0]==aiColor4D(1,1,1,.25f)
        &&vertexPair.scene->mMeshes[1]->mColors[0][0]==aiColor4D(.25f,.5f,.75f,.25f),
        "Replace appearance changed shared Modulate geometry colours");
    auto lower=modulate;lower.minFilter=lower.magFilter=1;
    auto upper=lower;upper.channel=3;upper.blendType=5;upper.pixelFormat=2;
    upper.pixels=QByteArray::fromHex("00ff004000ff004000ff004000ff0040");upper.total=upper.mipBytes=upper.pixels.size();
    TextureStackFixture::write(dir.filePath("vertex-stack.jt"),10,0,false,1,
        {{1,{}, {2},{21,20}}, {2,"vertex-alpha.jt",{}, {}}},{lower,upper});
    const auto stack=Jt::Importer::read(dir.filePath("vertex-stack.jt"));const auto* stackMesh=stack.scene->mMeshes[0];
    aiString stackPath;check(stack.scene->mMaterials[stackMesh->mMaterialIndex]->GetTexture(aiTextureType_DIFFUSE,0,&stackPath)==AI_SUCCESS,"Constant vertex stack lost composed image");
    const auto* stackTexture=stack.scene->GetEmbeddedTexture(stackPath.C_Str());check(stackTexture,"Constant stack image missing");
    const auto stackPixels=QImage::fromData(reinterpret_cast<const uchar*>(stackTexture->pcData),int(stackTexture->mWidth));
    // The legacy fixture's first bottom-row RGB texel is red; QImage row 1
    // contains that texel after the qualified JT image-row conversion.
    check(stackPixels.pixelColor(0,1)==QColor(137,255,0,64)&&stackMesh->mColors[0][0]==aiColor4D(1,1,1,.25f),
        "Constant vertex stack lost input, unit order or alpha product");
    upper.blendType=3;upper.pixelFormat=10;upper.pixels=QByteArray::fromHex("40404040");upper.total=upper.mipBytes=4;
    TextureStackFixture::write(dir.filePath("vertex-factor-stack.jt"),10,0,false,1,
        {{1,{}, {2},{20,21}}, {2,"vertex-alpha.jt",{}, {}}},{lower,upper});
    const auto factor=Jt::Importer::read(dir.filePath("vertex-factor-stack.jt"));const auto* factorMesh=factor.scene->mMeshes[0];
    aiString factorPath;check(factor.scene->mMaterials[factorMesh->mMaterialIndex]->GetTexture(aiTextureType_DIFFUSE,0,&factorPath)==AI_SUCCESS,"Factorized stack lost image");
    const auto* factorTexture=factor.scene->GetEmbeddedTexture(factorPath.C_Str());check(factorTexture,"Factorized stack pixels absent");
    const auto factorPixels=QImage::fromData(reinterpret_cast<const uchar*>(factorTexture->pcData),int(factorTexture->mWidth));
    check(factorPixels.pixelColor(0,1)==QColor(255,0,0,64)&&factorMesh->mColors[0][0]==aiColor4D(.25f,.5f,.75f,1),
        "Alpha Replace in a stack discarded vertex RGB or retained incoming alpha");
    geometry.materialColor={1,1,1};geometry.materialDataFlags=0x3990;
    LegacyFixture::write(dir.filePath("white-source.jt"),geometry);
    auto white=lower;white.pixels=QByteArray(12,char(255));white.total=white.mipBytes=12;
    auto product=white;product.channel=3;auto sum=product;sum.blendType=5;
    TextureStackFixture::write(dir.filePath("program-cache.jt"),10,0,false,1,
        {{1,{}, {2,3,4},{20}}, {2,"white-source.jt",{}, {21}},
         {3,"white-source.jt",{}, {22}}, {4,"white-source.jt",{}, {21}}},{white,product,sum});
    const auto programs=Jt::Importer::read(dir.filePath("program-cache.jt"));
    check(programs.scene->mNumMeshes==2,"Coincident material pixels aliased different vertex programs or lost identical-instance sharing");
    check(programs.scene->mMeshes[0]->mColors[0][0]==aiColor4D(.25f,.5f,.75f,.25f)
        &&programs.scene->mMeshes[1]->mColors[0][0]==aiColor4D(1,1,1,.25f),
        "Program cache reused Modulate colours for Add on shared geometry");
    std::cout<<"Inline RGB/RGBA Replace colours, opacity, endian/family mapping, variants and bounded fallback passed\n";
}
