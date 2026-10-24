// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original model-linear plane records and known local-coordinate expectations.
#include "JtTextureFixture.h"
#include "JtLegacyFixture.h"
#include "Jt/JtImporter.h"
#include "SceneUtils.h"
#include <QTemporaryDir>
#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
#include <cmath>
#include <iostream>
#include <limits>
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool near(float a,float b){return std::abs(a-b)<1e-5f;}
void compare(const aiMesh& mesh,const aiMesh& source,const TextureFixture::Options& o){
    const auto channel=unsigned(o.coordinate);
    check(mesh.HasTextureCoords(channel)&&mesh.mNumVertices==source.mNumVertices&&mesh.mNumFaces==source.mNumFaces,"Planar generation lost geometry/selected UV channel");
    for(unsigned i=0;i<mesh.mNumVertices;++i){
        const auto& p=source.mVertices[i];const auto& uv=mesh.mTextureCoords[channel][i];
        check(mesh.mVertices[i]==p&&mesh.mNormals[i]==source.mNormals[i],"Planar generation moved vertices/normals");
        const double s=o.generation[0]?p.x+o.planes[3]:source.mTextureCoords[channel][i].x;
        const double t=o.generation[1]?p.y+o.planes[7]:source.mTextureCoords[channel][i].y;
        // Tests use identity or diagonal scaling/translation after plane generation.
        check(near(uv.x,float(s*o.transform[0]+o.transform[12]))&&near(uv.y,float(t*o.transform[5]+o.transform[13])),"Plane equation, local space or transform order changed");
    }
    for(unsigned i=0;i<mesh.mNumFaces;++i)for(unsigned j=0;j<3;++j)
        check(mesh.mFaces[i].mIndices[j]==source.mFaces[i].mIndices[j],"Planar generation changed topology");
}
}
void texturePlanarTests(){
    QTemporaryDir dir;check(dir.isValid(),"Cannot create planar test directory");
    LegacyFixture::Options geometry;geometry.quantizedData=true;geometry.quantizedBits=4;
    LegacyFixture::write(dir.filePath("geometry.jt"),geometry);
    geometry.uvBinding=1;LegacyFixture::write(dir.filePath("stored.jt"),geometry);
    const auto plain=Jt::Importer::read(dir.filePath("geometry.jt"));const auto stored=Jt::Importer::read(dir.filePath("stored.jt"));
    check(!plain.scene->mMeshes[0]->HasTextureCoords(0),"Planar no-UV control unexpectedly has UVs");
    for(unsigned minor:{0u,3u,5u,7u,8u,9u,10u})for(bool big:{false,true}){
        TextureFixture::Options o;o.coordinate=0;o.big=big;o.baseVersion=minor?2:1;o.wrapped=minor!=0;
        o.generation={1,1,0,0};o.planes[0]=1;o.planes[5]=1;
        for(bool withStored:{false,true})for(unsigned channel:{0u,2u}){
            o.coordinate=int(channel);o.transform[0]=2;o.transform[5]=0.5f;o.transform[12]=0.25f;o.transform[13]=-0.125f;
            const QString file=withStored?"stored.jt":"geometry.jt";TextureFixture::write(dir.filePath("planes.jt"),minor,o,file);
            const auto result=Jt::Importer::read(dir.filePath("planes.jt"));const auto& mesh=*result.scene->mMeshes[0];
            check(result.scene->mNumMeshes==1&&result.scene->mNumTextures==1,"Planar image/mesh missing");
            compare(mesh,*plain.scene->mMeshes[0],o);
            const auto* material=result.scene->mMaterials[mesh.mMaterialIndex];unsigned selected=99;
            check(material->Get(AI_MATKEY_UVWSRC(aiTextureType_DIFFUSE,0),selected)==AI_SUCCESS&&selected==channel,"Generated coordinates selected wrong material set");
            for(unsigned c=0;c<channel;++c)check(mesh.HasTextureCoords(c),"Generated sparse UV prefix was not padded");
            std::unique_ptr<aiScene> copy(SceneUtils::deepCopyScene(result.scene.get()));check(bool(copy),"Cannot copy planar scene");compare(*copy->mMeshes[0],*plain.scene->mMeshes[0],o);
            if(minor==0&&!big&&!withStored){
                const auto path=dir.filePath("planes.glb").toStdString();Assimp::Exporter exporter;
                check(exporter.Export(result.scene.get(),"glb2",path)==AI_SUCCESS,"Cannot export generated UVs");
                Assimp::Importer importer;const auto* loaded=importer.ReadFile(path,0);
                check(loaded&&loaded->mNumMeshes==1&&loaded->mMeshes[0]->HasTextureCoords(channel),"GLB lost generated UV channel");
                for(unsigned i=0;i<mesh.mNumVertices;++i){const auto a=mesh.mTextureCoords[channel][i],b=loaded->mMeshes[0]->mTextureCoords[channel][i];check(near(a.x,b.x)&&near(a.y,b.y),"Planar GLB UVs changed");}
            }
        }
        o.coordinate=0;o.transform=TextureFixture::Options{}.transform;
        // Mixed stored/generated components use the stored array only where needed.
        for(unsigned component:{0u,1u}){
            o.generation={0,0,0,0};o.generation[component]=1;
            TextureFixture::write(dir.filePath("mixed.jt"),minor,o,"stored.jt");
            const auto result=Jt::Importer::read(dir.filePath("mixed.jt"));compare(*result.scene->mMeshes[0],*stored.scene->mMeshes[0],o);
            TextureFixture::write(dir.filePath("missing.jt"),minor,o);
            const auto missing=Jt::Importer::read(dir.filePath("missing.jt"));
            check(missing.scene->mNumMeshes==1&&!missing.warnings.isEmpty()&&!missing.scene->mMeshes[0]->HasTextureCoords(0),"Partial generation without stored UVs failed silently");
        }
        o.generation={1,1,0,0};auto second=o;second.planes[3]=0.25f;
        TextureFixture::writePair(dir.filePath("pair.jt"),minor,o,second);
        const auto pair=Jt::Importer::read(dir.filePath("pair.jt"));
        check(pair.scene->mNumMeshes==2&&pair.scene->mNumTextures==1,"Planar variants aliased meshes or duplicated image pixels");
        compare(*pair.scene->mMeshes[0],*plain.scene->mMeshes[0],o);compare(*pair.scene->mMeshes[1],*plain.scene->mMeshes[0],second);
        for(unsigned damage=0;damage<4;++damage){
            auto bad=o;if(damage==0)bad.generation[0]=2;if(damage==1)bad.generation[2]=2;
            if(damage==2)bad.planes[0]=2;if(damage==3){bad.planes[3]=std::numeric_limits<float>::max();bad.transform[0]=2;}
            TextureFixture::write(dir.filePath("unsupported.jt"),minor,bad);
            const auto result=Jt::Importer::read(dir.filePath("unsupported.jt"));const auto& mesh=*result.scene->mMeshes[0];
            check(result.scene->mNumMeshes==1&&!result.warnings.isEmpty()&&!mesh.HasTextureCoords(0),"Unsupported planar mapping lost geometry or leaked UVs");
            check(result.scene->mMaterials[mesh.mMaterialIndex]->GetTextureCount(aiTextureType_DIFFUSE)==0,"Unsupported planar mapping kept active image");
        }
    }
    std::cout<<"Model-linear texture planes, mixed/generated channels, sharing, copy/export and fallback passed\n";
}
