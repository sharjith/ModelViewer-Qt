// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original border images, literal levels and geometric clamp invariants.
#include "JtTextureStackFixture.h"
#include "JtLegacyFixture.h"
#include "Jt/JtImporter.h"
#include "SceneUtils.h"
#include "../../include/Materials/TextureMipChain.h"
#include <QTemporaryDir>
#include <QColor>
#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
#include <cmath>
#include <iostream>

namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
QImage source(int w,int h,QColor color,bool ring){
    QImage im(w+2*int(ring),h+2*int(ring),QImage::Format_RGBA8888);im.fill(QColor(255,0,255,64));
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)im.setPixelColor(x+int(ring),y+int(ring),color);
    return im;
}
QByteArray raw(const QImage& im){QByteArray out;for(int y=im.height()-1;y>=0;--y)out.append(reinterpret_cast<const char*>(im.constScanLine(y)),im.width()*4);return out;}
QImage image(const aiScene& scene){
    check(scene.mNumMeshes==1,"Border batch changed mesh count");aiString path;
    check(scene.mMaterials[scene.mMeshes[0]->mMaterialIndex]->GetTexture(aiTextureType_DIFFUSE,0,&path)==AI_SUCCESS,"Border batch lost texture");
    const auto* t=scene.GetEmbeddedTexture(path.C_Str());check(t,"Border batch embedded PNG missing");
    return QImage::fromData(reinterpret_cast<const uchar*>(t->pcData),int(t->mWidth),"PNG");
}
double area(const aiMesh& mesh){
    double sum=0;
    for(unsigned i=0;i<mesh.mNumFaces;++i){const auto& f=mesh.mFaces[i];
        const auto& a=mesh.mVertices[f.mIndices[0]];const auto& b=mesh.mVertices[f.mIndices[1]];const auto& c=mesh.mVertices[f.mIndices[2]];
        const double x=double(b.x)-a.x,y=double(b.y)-a.y,z=double(b.z)-a.z;
        const double u=double(c.x)-a.x,v=double(c.y)-a.y,w=double(c.z)-a.z;
        sum+=std::hypot(y*w-z*v,z*u-x*w,x*v-y*u)*.5;
    }
    return sum;
}
}
void textureBorderBatchTests(){
    QTemporaryDir dir;check(dir.isValid(),"Cannot create border batch directory");
    LegacyFixture::Options geometry;geometry.quantizedData=true;geometry.quantizedBits=4;geometry.uvBinding=1;geometry.materialDataFlags=0x3990;
    LegacyFixture::write(dir.filePath("geometry.jt"),geometry);const auto baseline=Jt::Importer::read(dir.filePath("geometry.jt"));
    for(unsigned family:{9u,10u})for(unsigned minor:family==9?std::vector<unsigned>{0,5}:std::vector<unsigned>{0,3,5,7,8,9,10})
    for(unsigned version:family==9?std::vector<unsigned>{1,2,3}:std::vector<unsigned>{1})for(bool big:{false,true}){
        const auto read=[&](TextureFixture::Options o){
            TextureStackFixture::write(dir.filePath("border-batch.jt"),family,minor,big,version,
                {{1,{}, {2},{20}}, {2,"geometry.jt",{}, {}}},{o});return Jt::Importer::read(dir.filePath("border-batch.jt"));
        };
        TextureFixture::Options o;o.coordinate=0;o.pixelFormat=2;o.minFilter=6;o.magFilter=2;
        o.borderMode=2;o.imageBorder=1;o.width=o.height=6;
        o.pixels=raw(source(4,4,Qt::red,true));o.total=o.mipBytes=o.pixels.size();
        o.extraMips={raw(source(2,2,Qt::green,true)),raw(source(1,1,Qt::yellow,true))};
        for(unsigned axes:{1u,2u,3u}){
            auto a=o;const bool s=axes&1,t=axes&2;a.wrapS=s?5:3;a.wrapT=t?5:2;
            const auto result=read(a);auto base=image(*result.scene);const auto levels=TextureMipChain::decode(base);
            check(base.size()==QSize(s?12:4,t?12:4)&&levels.size()==2&&levels.back().size()==QSize(s?3:1,t?3:1),"Mixed-axis authored border dimensions differ");
            check(levels.back().pixelColor(s?1:0,t?1:0)==QColor(Qt::yellow),"Mixed-axis terminal interior changed");
            check(levels.back().pixelColor(0,0)==QColor(255,0,255,64),"Mixed-axis ring was cropped on the sampled axis");
        }
        auto generated=o;generated.width=generated.height=4;generated.borderMode=1;generated.imageBorder=0;generated.wrapS=generated.wrapT=5;
        generated.extraMips.clear();generated.borderColor={1,0,1,.25f};
        auto checker=source(4,4,Qt::red,false);
        for(int y=0;y<4;++y)for(int x=0;x<4;++x)if((x+y)%2)checker.setPixelColor(x,y,Qt::green);
        generated.pixels=raw(checker);generated.total=generated.mipBytes=generated.pixels.size();
        const auto generatedResult=read(generated);auto generatedBase=image(*generatedResult.scene);const auto generatedTail=TextureMipChain::decode(generatedBase);
        check(generatedTail.size()==2&&generatedTail.back().pixelColor(1,1)==QColor(128,128,0,255)
            &&generatedTail.back().pixelColor(0,0)==QColor(255,0,255,64),"Generated constant border bled into the interior or changed alpha");
        auto rectangular=o;rectangular.width=10;rectangular.height=7;rectangular.wrapS=5;rectangular.wrapT=3;rectangular.extraMips.clear();
        auto ring=source(8,5,Qt::green,true);
        for(int y=1;y<=5;++y)ring.setPixelColor(0,y,(y%2)?QColor(255,0,0,64):QColor(0,0,255,64));
        rectangular.pixels=raw(ring);rectangular.total=rectangular.mipBytes=rectangular.pixels.size();
        const auto rectangle=read(rectangular);auto rectBase=image(*rectangle.scene);auto rectTail=TextureMipChain::decode(rectBase);
        check(rectBase.size()==QSize(24,5)&&rectTail.size()==3&&rectTail[0].size()==QSize(12,2)&&rectTail.back().size()==QSize(3,1),"Rectangular/NPOT unsampled axis chain dimensions differ");
        check(rectTail.back().pixelColor(0,0)==QColor(153,0,102,64)&&rectTail.back().pixelColor(1,0)==QColor(Qt::green),"Separate odd-length ring reduction differs from literal area average");
        check(rectBase.text(QLatin1String(TextureMipChain::TerminalKey))==QLatin1String(TextureMipChain::BorderSTerminal),"Single-axis terminal marker missing");
        check(ring.save(dir.filePath("original_texture.png")),"Cannot author external ring");
        auto external=rectangular;external.inlineImage=false;
        const auto externalResult=read(external);auto externalBase=image(*externalResult.scene);auto externalTail=TextureMipChain::decode(externalBase);
        check(externalBase==rectBase&&externalTail.back()==rectTail.back(),"External ring differs from equivalent inline ring");
        for(bool explicitRing:{false,true})for(int filter:{1,2}){
            auto legacy=o;legacy.extraMips.clear();legacy.wrapS=legacy.wrapT=1;legacy.minFilter=legacy.magFilter=filter;
            legacy.borderMode=explicitRing?2:1;legacy.imageBorder=explicitRing?1:0;legacy.width=legacy.height=2+2*int(explicitRing);
            legacy.borderColor={1,0,1,.25f};legacy.pixels=raw(source(2,2,Qt::green,explicitRing));legacy.total=legacy.mipBytes=legacy.pixels.size();
            legacy.transform[0]=legacy.transform[5]=2;legacy.transform[12]=legacy.transform[13]=-.5f;
            const auto clamped=read(legacy);const auto& mesh=*clamped.scene->mMeshes[0];
            check(mesh.mNumFaces>baseline.scene->mMeshes[0]->mNumFaces&&std::abs(area(mesh)-area(*baseline.scene->mMeshes[0]))<1e-5,"Legacy clamp did not refine UV crossings or changed surface area");
            for(unsigned v=0;v<mesh.mNumVertices;++v){const auto uv=mesh.mTextureCoords[0][v];check(uv.x>=.25f&&uv.x<=.75f&&uv.y>=.25f&&uv.y<=.75f,"Legacy clamp vertices escaped the interior interval");}
        }
        auto rejected=rectangular;rejected.wrapT=5;
        const auto failed=read(rejected);
        check(!failed.warnings.isEmpty()&&failed.scene->mMaterials[failed.scene->mMeshes[0]->mMaterialIndex]->GetTextureCount(aiTextureType_DIFFUSE)==0,"Unrepresentable two-axis rectangle was silently approximated");
        if(family==10&&minor==0&&!big){
            std::unique_ptr<aiScene> copy(SceneUtils::deepCopyScene(rectangle.scene.get()));check(TextureMipChain::decode(image(*copy)).back()==rectTail.back(),"Scene copy lost single-axis terminal level");
            Assimp::Exporter exporter;const auto path=dir.filePath("border-batch.glb").toStdString();
            check(exporter.Export(rectangle.scene.get(),"glb2",path)==AI_SUCCESS,"Border batch GLB export failed");
            Assimp::Importer importer;const auto* glb=importer.ReadFile(path,0);check(glb&&TextureMipChain::decode(image(*glb)).back()==rectTail.back(),"GLB lost rectangular border terminal");
        }
    }
    std::cout<<"Mixed-axis/authored/generated/external borders, rectangular profiles and portable legacy clamp checks passed\n";
}
