// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#include "JtTextureStackFixture.h"
#include "JtLegacyFixture.h"
#include "Jt/JtImporter.h"
#include "SceneUtils.h"
#include "../../include/Materials/TextureMipChain.h"
#include <QTemporaryDir>
#include <QColor>
#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
#include <assimp/GltfMaterial.h>
#include <iostream>
#include <cmath>

namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
QImage image(const aiScene& scene){
    check(scene.mNumMeshes==1,"Mip batch changed geometry count");aiString path;
    check(scene.mMaterials[scene.mMeshes[0]->mMaterialIndex]->GetTexture(aiTextureType_DIFFUSE,0,&path)==AI_SUCCESS,"Mip batch lost texture");
    const auto* t=scene.GetEmbeddedTexture(path.C_Str());check(t&&t->mHeight==0,"Mip batch PNG absent");
    return QImage::fromData(reinterpret_cast<const uchar*>(t->pcData),int(t->mWidth),"PNG");
}
QByteArray pixels(int interior,QColor color,bool border){
    QImage im(interior+2*int(border),interior+2*int(border),QImage::Format_RGBA8888);im.fill(Qt::magenta);
    for(int y=0;y<interior;++y)for(int x=0;x<interior;++x)im.setPixelColor(x+int(border),y+int(border),color);
    QByteArray bytes;
    for(int y=im.height()-1;y>=0;--y)bytes.append(reinterpret_cast<const char*>(im.constScanLine(y)),im.width()*4);
    return bytes;
}
TextureFixture::Options texture(bool ring=false){
    TextureFixture::Options o;o.coordinate=0;o.width=o.height=4+2*int(ring);o.pixelFormat=2;o.alignment=1;
    o.minFilter=3;o.magFilter=1;o.wrapS=o.wrapT=4;o.imageBorder=ring?1:0;o.borderMode=ring?2:0;
    o.pixels=pixels(4,Qt::red,ring);o.total=o.mipBytes=int(o.pixels.size());
    o.extraMips={pixels(2,Qt::green,ring),pixels(1,Qt::yellow,ring)};return o;
}
}
void textureMipBatchTests(){
    QTemporaryDir dir;check(dir.isValid(),"Cannot create mip batch directory");
    LegacyFixture::Options geometry;geometry.quantizedData=true;geometry.quantizedBits=4;geometry.uvBinding=1;
    geometry.materialAlpha=1;geometry.materialDataFlags=0x3990;
    LegacyFixture::write(dir.filePath("geometry.jt"),geometry);
    const auto baseline=Jt::Importer::read(dir.filePath("geometry.jt"));
    using Node=TextureStackFixture::Node;
    for(unsigned family:{9u,10u})for(unsigned minor:family==9?std::vector<unsigned>{0,5}:std::vector<unsigned>{0,3,5,7,8,9,10})
    for(unsigned version:family==9?std::vector<unsigned>{1,2,3}:std::vector<unsigned>{1})for(bool big:{false,true}){
        auto read=[&](std::vector<TextureFixture::Options> attrs){
            std::vector<unsigned> ids;for(unsigned i=0;i<attrs.size();++i)ids.push_back(20+i);
            TextureStackFixture::write(dir.filePath("batch.jt"),family,minor,big,version,{{1,{}, {2},ids},{2,"geometry.jt",{}, {}}},attrs);
            return Jt::Importer::read(dir.filePath("batch.jt"));
        };
        auto a=texture(),b=texture();a.blendType=3;b.blendType=5;b.channel=3;
        b.pixels=pixels(4,QColor(0,0,255,128),false);b.extraMips={pixels(2,QColor(0,0,255,128),false),pixels(1,QColor(0,0,255,128),false)};
        auto combined=read({a,b});auto base=image(*combined.scene);auto tail=TextureMipChain::decode(base);
        check(base.pixelColor(0,0)==QColor(255,0,255,128)&&tail.size()==2&&tail[0].pixelColor(0,0)==QColor(0,255,255,128)&&tail[1].pixelColor(0,0)==QColor(255,255,255,128),"Composed authored mip colours/alpha differ");
        for(int mode:{1,4,5}){
            auto single=texture();single.blendType=mode;single.blendColor={1,1,1,1};
            auto result=read({single});auto level0=image(*result.scene);auto levels=TextureMipChain::decode(level0);
            check(levels.size()==2&&level0.pixelColor(0,0)==QColor(mode==1?Qt::red:Qt::magenta)&&levels[0].pixelColor(0,0)==QColor(mode==1?Qt::green:Qt::cyan)&&levels[1].pixelColor(0,0)==QColor(mode==1?Qt::yellow:Qt::white),"Single combiner changed across mip levels");
        }
        for(bool explicitRing:{false,true})for(unsigned filter:{3u,6u}){
            auto o=texture(explicitRing);o.borderMode=explicitRing?2:1;o.wrapS=o.wrapT=5;o.borderColor={1,0,1,1};o.minFilter=filter;o.magFilter=filter==6?2:1;
            auto result=read({o});auto full=image(*result.scene);auto levels=TextureMipChain::decode(full);
            check(full.size()==QSize(12,12)&&levels.size()==2&&levels[0].size()==QSize(6,6)&&levels[1].size()==QSize(3,3),"Border mip footprint is not level invariant");
            check(full.pixelColor(0,0)==QColor(Qt::magenta)&&levels[1].pixelColor(0,0)==QColor(Qt::magenta)&&levels[1].pixelColor(1,1)==QColor(Qt::yellow),"Border ring or terminal interior changed");
            check(full.text(QLatin1String(TextureMipChain::TerminalKey))==QLatin1String(TextureMipChain::BorderTerminal),"Border max-level profile missing");
            const auto& old=*baseline.scene->mMeshes[0];const auto& mesh=*result.scene->mMeshes[0];
            check(mesh.mNumVertices==old.mNumVertices&&mesh.mNumFaces==old.mNumFaces,"Border mips changed topology");
            for(unsigned i=0;i<mesh.mNumVertices;++i){const auto uv=mesh.mTextureCoords[0][i],src=old.mTextureCoords[0][i];
                check(mesh.mVertices[i]==old.mVertices[i]&&std::abs(uv.x-(src.x+1)/3)<1e-6&&std::abs(uv.y-(src.y+1)/3)<1e-6,"Border mip mapping/positions changed");}
            if(family==10&&minor==0&&!big&&filter==3){
                std::unique_ptr<aiScene> copy(SceneUtils::deepCopyScene(result.scene.get()));check(TextureMipChain::decode(image(*copy)).size()==2,"Scene copy lost border levels");
                Assimp::Exporter exporter;const auto path=dir.filePath("border.glb").toStdString();check(exporter.Export(result.scene.get(),"glb2",path)==AI_SUCCESS,"Border carrier GLB export failed");
                Assimp::Importer importer;const auto* glb=importer.ReadFile(path,0);check(glb,"Border carrier GLB import failed");auto exported=TextureMipChain::decode(image(*glb));
                check(exported.size()==2&&exported.back().size()==QSize(3,3),"GLB carrier lost terminal profile");
            }
        }
        auto ringA=texture(true),ringB=texture(true);ringA.wrapS=ringA.wrapT=ringB.wrapS=ringB.wrapT=5;
        ringA.blendType=3;ringB.blendType=5;ringB.channel=3;
        ringB.pixels=pixels(4,Qt::blue,true);ringB.extraMips={pixels(2,Qt::blue,true),pixels(1,Qt::blue,true)};
        auto ringStack=read({ringA,ringB});auto ringImage=image(*ringStack.scene);auto ringTail=TextureMipChain::decode(ringImage);
        check(ringTail.size()==2&&ringImage.pixelColor(5,5)==QColor(Qt::magenta)&&ringTail[0].pixelColor(2,2)==QColor(Qt::cyan)&&ringTail[1].pixelColor(1,1)==QColor(Qt::white),"Composed border mip interiors changed");
        auto ignored=texture(true);auto ignoredScene=read({ignored});check(image(*ignoredScene.scene).size()==QSize(4,4)&&TextureMipChain::decode(image(*ignoredScene.scene)).back().size()==QSize(1,1),"Unsampled rings did not crop per source level");
        // Affine single-stage operations preserve filter selection. Literal
        // primary colours avoid deriving the oracle from the implementation.
        for(int mode:{1,4})for(int filter:{1,2,3,4,5,6}){
            auto o=texture();o.blendType=mode;o.minFilter=filter;o.magFilter=2;
            if(mode==1){
                o.pixelFormat=1;
                auto rgb=[](const QByteArray& rgba){QByteArray bytes;for(qsizetype i=0;i<rgba.size();i+=4)bytes.append(rgba.constData()+i,3);return bytes;};
                o.pixels=rgb(o.pixels);for(auto& mip:o.extraMips)mip=rgb(mip);
                o.total=o.mipBytes=int(o.pixels.size());
            }
            auto result=read({o});auto base=image(*result.scene);auto levels=TextureMipChain::decode(base);
            check(levels.size()==2&&base.pixelColor(0,0)==QColor(mode==1?Qt::red:Qt::blue)
                &&levels[1].pixelColor(0,0)==QColor(mode==1?Qt::yellow:Qt::blue),"Affine filtered combiner changed literal levels");
            int actual=0;const int expected[]={0,9728,9729,9984,9985,9986,9987};
            check(result.scene->mMaterials[result.scene->mMeshes[0]->mMaterialIndex]->Get(AI_MATKEY_GLTF_MAPPINGFILTER_MIN(aiTextureType_DIFFUSE,0),actual)==AI_SUCCESS&&actual==expected[filter],"Affine combiner lost minification filter");
        }
        // All previously accepted colour layouts use the same validated ring
        // recurrence; no format-specific RGBA-only mip envelope is needed.
        for(unsigned format:{1u,2u,3u,4u,7u,8u,9u,10u,11u,12u}){
            auto o=texture(true);o.pixelFormat=format;o.wrapS=o.wrapT=5;o.minFilter=6;o.magFilter=2;
            auto white=[&](int interior){const int width=interior+2;const int components=format==2||format==12?4:format==1||format==11?3:format==4?2:1;return QByteArray(width*width*components,char(255));};
            o.pixels=white(4);o.extraMips={white(2),white(1)};o.total=o.mipBytes=int(o.pixels.size());
            auto result=read({o});auto base=image(*result.scene);auto levels=TextureMipChain::decode(base);
            const QColor expected=format==7?QColor(Qt::red):format==8?QColor(Qt::green):format==9?QColor(Qt::blue):QColor(Qt::white);
            check(levels.size()==2&&base.pixelColor(0,0)==expected&&levels.back().pixelColor(1,1)==expected,"Colour-layout authored ring changed components");
        }
        auto reject=[&](std::vector<TextureFixture::Options> attrs){auto result=read(attrs);check(result.scene->mNumMeshes==1&&!result.warnings.isEmpty()&&result.scene->mMaterials[result.scene->mMeshes[0]->mMaterialIndex]->GetTextureCount(aiTextureType_DIFFUSE)==0,"Invalid mip stack/border did not diagnose geometry fallback");};
        auto bad=b;bad.extraMips.pop_back();reject({a,bad});
        bad=b;bad.minFilter=6;bad.magFilter=2;reject({a,bad});
        bad=texture();bad.blendType=1;bad.minFilter=6;bad.magFilter=2;reject({bad});
        bad=texture();bad.blendType=5;bad.minFilter=6;bad.magFilter=2;reject({bad});
        bad=texture(true);bad.wrapS=5;bad.wrapT=2;reject({bad});
        bad=texture(true);bad.wrapS=bad.wrapT=5;bad.extraMips[0].chop(1);reject({bad});
    }
    std::cout<<"Composed authored chains, border level mapping, copy/GLB, family/endian and diagnosed fallback checks passed\n";
}
