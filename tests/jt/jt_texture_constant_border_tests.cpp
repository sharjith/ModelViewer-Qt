// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original constant border pixels and hand-calculated 2x2-to-4x4 UV mapping.
#include "JtTextureFixture.h"
#include "JtLegacyFixture.h"
#include "Jt/JtImporter.h"
#include <QImage>
#include <QColor>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
QImage interior(){QImage image(2,2,QImage::Format_RGBA8888);image.setPixelColor(0,0,Qt::blue);image.setPixelColor(1,0,Qt::white);image.setPixelColor(0,1,Qt::red);image.setPixelColor(1,1,Qt::green);return image;}
TextureFixture::Options options(bool big=false){
    TextureFixture::Options o;o.big=big;o.coordinate=0;o.pixelFormat=2;o.borderMode=1;o.wrapS=o.wrapT=5;o.borderColor={1,0.5f,0,0.25f};o.pixels=QByteArray("");
    const auto image=interior();for(int y=1;y>=0;--y)for(int x=0;x<2;++x){const auto c=image.pixelColor(x,y);for(int v:{c.red(),c.green(),c.blue(),c.alpha()})o.pixels.append(char(v));}
    o.total=o.mipBytes=int(o.pixels.size());return o;
}
QImage textureImage(const Jt::ImportResult& result,const aiMesh& mesh){aiString path;check(result.scene->mMaterials[mesh.mMaterialIndex]->GetTexture(aiTextureType_DIFFUSE,0,&path)==AI_SUCCESS,"Constant border image association missing");const auto* image=result.scene->GetEmbeddedTexture(path.C_Str());check(image,"Constant border image missing");return QImage::fromData(reinterpret_cast<const uchar*>(image->pcData),int(image->mWidth)).convertToFormat(QImage::Format_RGBA8888);}
}
void constantBorderTests(){
    QTemporaryDir dir;check(dir.isValid(),"Cannot create constant-border test folder");
    LegacyFixture::Options geometry;geometry.quantizedData=true;geometry.quantizedBits=4;geometry.uvBinding=1;geometry.materialDataFlags=0x3990;LegacyFixture::write(dir.filePath("blend.jt"),geometry);
    geometry.materialDataFlags=0x3980;LegacyFixture::write(dir.filePath("opaque.jt"),geometry);const auto baseline=Jt::Importer::read(dir.filePath("opaque.jt"));
    for(unsigned family:{9u,10u})for(unsigned minor:family==9?std::vector<unsigned>{0,5}:std::vector<unsigned>{0,3,5,7,8,9,10})
    for(unsigned version:family==9?std::vector<unsigned>{1,2,3}:std::vector<unsigned>{1})for(bool big:{false,true}){
        const auto write=[&](TextureFixture::Options o,bool blend){const auto geometry=blend?"blend.jt":"opaque.jt";if(family==9)TextureFixture::write9(dir.filePath("border.jt"),minor,o,geometry);else TextureFixture::write(dir.filePath("border.jt"),minor,o,geometry);};
        auto base=options(big);base.version=version;base.legacyType=version==1?2:9;base.baseVersion=family==10&&minor?2:1;base.wrapped=family==10&&minor;
        for(unsigned axes:{1u,2u,3u})for(bool blend:{false,true})for(int filter:{1,2}){
            auto o=base;const bool s=axes&1,t=axes&2;o.wrapS=s?5:2;o.wrapT=t?5:4;o.minFilter=filter;write(o,blend);
            const auto result=Jt::Importer::read(dir.filePath("border.jt"));check(result.scene->mNumMeshes==1,"Constant border changed mesh count");
            const auto& mesh=*result.scene->mMeshes[0];const auto& source=*baseline.scene->mMeshes[0];const auto pixels=textureImage(result,mesh);
            QImage expected(s?4:2,t?4:2,QImage::Format_RGBA8888);expected.fill(QColor(255,128,0,blend?64:255));const auto image=interior();
            for(int y=0;y<2;++y)for(int x=0;x<2;++x)expected.setPixelColor(x+int(s),y+int(t),image.pixelColor(x,y));
            check(pixels==expected,"Constant border colour/alpha/axis placement differs");
            check(mesh.mNumVertices==source.mNumVertices&&mesh.mNumFaces==source.mNumFaces,"Constant border changed geometry counts");
            for(unsigned i=0;i<mesh.mNumVertices;++i){const auto old=source.mTextureCoords[0][i],uv=mesh.mTextureCoords[0][i];
                check(mesh.mVertices[i]==source.mVertices[i]&&std::abs(uv.x-(s?old.x*0.5f+0.25f:old.x))<1e-6f&&std::abs(uv.y-(t?old.y*0.5f+0.25f:old.y))<1e-6f,"Constant border interior-relative UV mapping differs");}
            aiString mode;const auto* material=result.scene->mMaterials[mesh.mMaterialIndex];
            check((material->Get("$mat.gltf.alphaMode",0,0,mode)==AI_SUCCESS&&mode==aiString("BLEND"))==blend,"Sampled border alpha selected wrong pass");
        }
        for(unsigned damage=0;damage<5;++damage){auto o=base;if(damage==0)o.minFilter=3;if(damage==1)o.borderMode=2;if(damage==2)o.borderMode=0;if(damage==3)o.wrapS=1;if(damage==4)o.pixelFormat=1;write(o,false);
            const auto result=Jt::Importer::read(dir.filePath("border.jt"));check(result.scene->mNumMeshes==1&&result.scene->mNumTextures==0&&!result.warnings.isEmpty(),"Unqualified sampled border did not retain geometry with diagnostic");}
    }
    check(interior().save(dir.filePath("original_texture.png")),"Cannot write RGBA border source");
    auto external=options();external.inlineImage=false;TextureFixture::write(dir.filePath("external.jt"),0,external,"opaque.jt");
    const auto loaded=Jt::Importer::read(dir.filePath("external.jt"));check(textureImage(loaded,*loaded.scene->mMeshes[0]).size()==QSize(4,4),"RGBA external constant border missing");
    // The same child image attribute inherits different colours from its two
    // parents. Its border field is inhibited: image identity alone cannot cache it.
    using namespace Fixture;Bytes graph(false);element(graph,type("10dd103e"),1,partition(false,{}, {2,3}));
    element(graph,type("10dd103e"),2,partition(false,{}, {4},{21}));element(graph,type("10dd103e"),3,partition(false,{}, {5},{22}));
    element(graph,type("10dd103e"),4,partition(false,"opaque.jt",{}, {20}));element(graph,type("10dd103e"),5,partition(false,"opaque.jt",{}, {20}));
    auto shared=options();shared.inhibit=2;auto first=options(),second=options();second.borderColor={0,1,1,1};
    element(graph,type("10dd1073"),20,TextureFixture::body(shared),3);element(graph,type("10dd1073"),21,TextureFixture::body(first),3);element(graph,type("10dd1073"),22,TextureFixture::body(second),3);
    end(graph);end(graph);graph.u16(1);graph.u32(0);container(dir.filePath("inherited.jt"),graph,0);
    const auto pair=Jt::Importer::read(dir.filePath("inherited.jt"));check(pair.scene->mNumMeshes==2&&pair.scene->mNumTextures==2,"Inherited border appearances aliased");
    const auto a=textureImage(pair,*pair.scene->mMeshes[0]),b=textureImage(pair,*pair.scene->mMeshes[1]);check(a!=b,"Shared child image cache ignored inherited border colour");
    std::cout<<"Constant RGBA border sampling, UV baking, alpha, family/endian mapping and shared inheritance passed\n";
}
