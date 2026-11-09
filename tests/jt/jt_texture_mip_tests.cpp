// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#include "JtTextureFixture.h"
#include "JtLegacyFixture.h"
#include "SceneUtils.h"
#include "Jt/JtImporter.h"
#include "../../include/Materials/TextureMipChain.h"
#include <QTemporaryDir>
#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
#include <iostream>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
QImage baseImage(const aiScene& scene,unsigned meshIndex=0){
    check(scene.mNumMeshes>0,"Mip scene lost geometry");aiString path;
    check(scene.mMaterials[scene.mMeshes[meshIndex]->mMaterialIndex]->GetTexture(aiTextureType_DIFFUSE,0,&path)==AI_SUCCESS,"Mip texture missing");
    const auto* texture=scene.GetEmbeddedTexture(path.C_Str());check(texture&&texture->mHeight==0,"Mip PNG carrier missing");
    return QImage::fromData(reinterpret_cast<const uchar*>(texture->pcData),int(texture->mWidth),"PNG");
}
void verify(const aiScene& scene,QColor expected=Qt::yellow,unsigned meshIndex=0){
    auto base=baseImage(scene,meshIndex);auto levels=TextureMipChain::decode(base);
    check(base.size()==QSize(4,4)&&base.pixelColor(0,0)==QColor(Qt::red),"Mip base pixels changed");
    check(levels.size()==2&&levels[0].size()==QSize(2,2)&&levels[0].pixelColor(0,0)==QColor(Qt::green)
        &&levels[1].size()==QSize(1,1)&&levels[1].pixelColor(0,0)==expected,"Authored levels were lost, reordered or regenerated");
}
}
void textureMipTests(){
    QTemporaryDir temp;check(temp.isValid(),"Cannot make mip directory");
    LegacyFixture::Options geometry;geometry.quantizedData=true;geometry.quantizedBits=4;geometry.uvBinding=1;
    LegacyFixture::write(temp.filePath("geometry.jt"),geometry);
    auto solid=[](int pixels,unsigned char r,unsigned char g,unsigned char b){QByteArray data;for(int i=0;i<pixels;++i){data.append(char(r));data.append(char(g));data.append(char(b));}return data;};
    for(unsigned family:{9u,10u})for(unsigned minor:(family==9?std::vector<unsigned>{0,5}:std::vector<unsigned>{0,3,5,7,8,9,10}))
      for(bool big:{false,true})for(int alignment:{1,4,8})for(unsigned local:(family==9?std::vector<unsigned>{1,2,3}:std::vector<unsigned>{1})){
        TextureFixture::Options options;options.big=big;options.coordinate=0;options.width=options.height=4;
        auto padded=[&](int width,int height,unsigned char r,unsigned char g,unsigned char b){
            auto row=solid(width,r,g,b);while(row.size()%alignment)row.append(char(0));QByteArray data;for(int y=0;y<height;++y)data+=row;return data;};
        options.alignment=alignment;options.pixels=padded(4,4,255,0,0);options.total=options.mipBytes=int(options.pixels.size());
        options.extraMips={padded(2,2,0,255,0),padded(1,1,255,255,0)};
        options.blendType=alignment==8?3:2;
        options.version=local;options.legacyType=local==1?2:9;options.minFilter=3;options.baseVersion=family==10&&minor!=0?2:1;options.wrapped=family==10&&minor!=0;
        const auto path=temp.filePath("mips.jt");
        auto write=[&](TextureFixture::Options value){if(family==9)TextureFixture::write9(path,minor,value);else TextureFixture::write(path,minor,value);};
        write(options);auto result=Jt::Importer::read(path);verify(*result.scene);
        check(result.scene->mMeshes[0]->HasTextureCoords(0),"Mip support removed UVs");
        // Same base pixels, different authored tail must not share the display resource.
        options.extraMips[1]=padded(1,1,0,0,255);write(options);auto alternate=Jt::Importer::read(path);verify(*alternate.scene,Qt::blue);
        auto rejected=[&](TextureFixture::Options bad){write(bad);auto fallback=Jt::Importer::read(path);
            check(fallback.scene->mNumMeshes>0&&!fallback.warnings.isEmpty(),"Invalid mip did not preserve geometry with a diagnostic");
            check(fallback.scene->mMaterials[fallback.scene->mMeshes[0]->mMaterialIndex]->GetTextureCount(aiTextureType_DIFFUSE)==0,"Invalid mip silently mapped base level");};
        auto bad=options;bad.extraMips.pop_back();rejected(bad); // incomplete tail
        bad=options;bad.extraMips[0].chop(1);rejected(bad); // wrong stride
        bad=options;bad.extraMips.push_back(padded(1,1,0,0,0));rejected(bad); // beyond 1x1
        bad=options;bad.imageBorder=1;bad.borderMode=2;bad.wrapS=bad.wrapT=5;rejected(bad); // unqualified border mips
        if(family==10&&minor==0&&!big&&alignment==1){
            auto first=options;first.extraMips[1]=padded(1,1,255,255,0);
            TextureFixture::writePair(temp.filePath("pair.jt"),0,first,options);
            const auto pair=Jt::Importer::read(temp.filePath("pair.jt"));check(pair.scene->mNumMeshes==2,"Shared geometry mip variants merged");
            verify(*pair.scene,Qt::yellow,0);verify(*pair.scene,Qt::blue,1);
            std::unique_ptr<aiScene> copy(SceneUtils::deepCopyScene(result.scene.get()));verify(*copy);
            Assimp::Exporter exporter;const auto output=temp.filePath("mips.glb").toStdString();
            check(exporter.Export(result.scene.get(),"glb2",output)==AI_SUCCESS,"Mip GLB export failed");
            Assimp::Importer importer;const auto* glb=importer.ReadFile(output,0);check(glb,"Mip GLB import failed");verify(*glb);
        }
    }
    std::cout<<"Authored mip image/family/endian, fallback, scene-copy and PNG-in-GLB checks passed\n";
}
