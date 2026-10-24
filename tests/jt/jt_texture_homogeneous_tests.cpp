// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original known-value homogeneous reductions; no vendor model/reference data.
#include "JtTextureFixture.h"
#include "JtLegacyFixture.h"
#include "JtTextureMapping.h"
#include "Jt/JtImporter.h"
#include "SceneUtils.h"
#include <QTemporaryDir>
#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
#include <cmath>
#include <iostream>

namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
bool near(float a,float b){return std::abs(a-b)<1e-5f;}
void expected(const aiMesh& mesh,const aiMesh& source,unsigned recipe,unsigned channel=0){
    check(mesh.mNumVertices==source.mNumVertices&&mesh.mNumFaces==source.mNumFaces,"Homogeneous mapping changed topology");
    check(mesh.HasTextureCoords(channel),"Homogeneous mapping lost its selected coordinate channel");
    for(unsigned i=0;i<mesh.mNumVertices;++i){
        const auto& uv=source.mTextureCoords[0][i];const auto& p=source.mVertices[i];
        const float s=recipe==0?uv.x*.5f:recipe==1?-uv.x*.5f:recipe==2?(uv.x+p.x)*.5f:p.x*.5f;
        const float t=recipe==0?uv.y*.5f:recipe==1?-uv.y*.5f:recipe==2?uv.y*.5f:p.y*.5f;
        check(mesh.mVertices[i]==p&&mesh.mNormals[i]==source.mNormals[i],"Homogeneous mapping moved source geometry");
        check(near(mesh.mTextureCoords[channel][i].x,s)&&near(mesh.mTextureCoords[channel][i].y,t),"Homogeneous mapping known-value mismatch");
    }
}
void directFaceProof(){
    aiMesh mesh;mesh.mNumVertices=6;mesh.mVertices=new aiVector3D[6]{{0,0,0},{1,0,0},{0,1,0},{0,0,2},{1,0,2},{0,1,2}};
    mesh.mTextureCoords[0]=new aiVector3D[6]{{0,0,0},{1,0,0},{0,1,0},{0,0,0},{1,0,0},{0,1,0}};
    mesh.mNumUVComponents[0]=2;mesh.mNumFaces=2;mesh.mFaces=new aiFace[2];
    for(unsigned i=0;i<2;++i){mesh.mFaces[i].mNumIndices=3;mesh.mFaces[i].mIndices=new unsigned[3]{3*i,3*i+1,3*i+2};}
    std::array<int,4> gen{0,0,0,1};std::array<float,16> planes{};planes[14]=1;planes[15]=1;
    auto matrix=TextureFixture::Options{}.transform;
    const auto mapped=Jt::TextureMapping::map(mesh,0,matrix,gen,planes,{1,1,0,0},[]{});
    check(mapped[1].x==1&&near(mapped[4].x,1.f/3)&&mapped[2].y==1&&near(mapped[5].y,1.f/3),"Disconnected faces lost their independent constant Q");
    check(mesh.mTextureCoords[0][4].x==1,"Homogeneous proof changed source UVs");
    // Even tiny nonzero variation is rejected, never hidden by a tolerance.
    gen={0,0,0,0};
    for(float tiny:{1e-7f,1e-30f}){
        matrix[3]=tiny;bool rejected=false;
        try{Jt::TextureMapping::map(mesh,0,matrix,gen,planes,{1,1,0,0},[]{});}catch(const std::runtime_error&){rejected=true;}
        check(rejected,"Near-constant Q was approximated or hidden in offset rounding");
    }
}
}
void textureHomogeneousTests(){
    directFaceProof();QTemporaryDir dir;check(dir.isValid(),"Cannot create homogeneous control directory");
    LegacyFixture::Options geometry;geometry.quantizedData=true;geometry.quantizedBits=4;geometry.uvBinding=1;
    LegacyFixture::write(dir.filePath("geometry.jt"),geometry);
    auto noUV=geometry;noUV.uvBinding=0;LegacyFixture::write(dir.filePath("no_uv.jt"),noUV);
    const auto original=Jt::Importer::read(dir.filePath("geometry.jt"));const auto& source=*original.scene->mMeshes[0];
    const auto write=[&](const QString& path,unsigned family,unsigned minor,TextureFixture::Options o,bool missingUV=false){
        const QString companion=missingUV?"no_uv.jt":"geometry.jt";
        if(family==9)TextureFixture::write9(path,minor,o,companion);else TextureFixture::write(path,minor,o,companion);
    };
    for(unsigned family:{9u,10u})for(unsigned minor:(family==9?std::vector<unsigned>{0,5}:std::vector<unsigned>{0,3,5,7,8,9,10}))for(bool big:{false,true}){
        const auto versions=family==9?std::vector<unsigned>{1,2,3}:std::vector<unsigned>{1};
        for(unsigned version:versions){
            TextureFixture::Options o;o.coordinate=0;o.big=big;o.version=version;o.legacyType=version==1?2:9;
            if(family==10){o.baseVersion=minor?2:1;o.wrapped=minor!=0;}
            for(unsigned recipe=0;recipe<4;++recipe){
                o.transform=TextureFixture::Options{}.transform;o.generation={};o.planes={};o.transform[15]=recipe==1?-2:2;
                o.coordinate=recipe==3&&!(family==9&&version==1)?2:0;
                if(recipe==0){o.transform[2]=7;o.transform[14]=3;}
                if(recipe==2){o.generation[2]=1;o.planes[8]=1;o.transform[8]=1;}
                if(recipe==3){o.generation={1,1,0,1};o.planes[0]=1;o.planes[5]=1;o.planes[14]=1;o.planes[15]=2;o.transform[15]=1;}
                write(dir.filePath("homogeneous.jt"),family,minor,o,recipe==3);
                const auto result=Jt::Importer::read(dir.filePath("homogeneous.jt"));const auto& mesh=*result.scene->mMeshes[0];
                check(result.scene->mMaterials[mesh.mMaterialIndex]->GetTextureCount(aiTextureType_DIFFUSE)==1,"Exact homogeneous mapping lost its texture");
                expected(mesh,source,recipe,unsigned(o.coordinate));
                std::unique_ptr<aiScene> copy(SceneUtils::deepCopyScene(result.scene.get()));expected(*copy->mMeshes[0],source,recipe,unsigned(o.coordinate));
                if(family==10&&minor==0&&!big){
                    const auto path=dir.filePath("homogeneous.glb").toStdString();Assimp::Exporter exporter;
                    check(exporter.Export(result.scene.get(),"glb2",path)==AI_SUCCESS,"Cannot export homogeneous UVs");
                    Assimp::Importer importer;const auto* loaded=importer.ReadFile(path,0);
                    check(loaded&&loaded->mNumMeshes==1,"Homogeneous GLB lost geometry");expected(*loaded->mMeshes[0],source,recipe,unsigned(o.coordinate));
                }
            }
            for(unsigned damage=0;damage<5;++damage){
                o.coordinate=0;
                o.transform=TextureFixture::Options{}.transform;o.generation={};o.planes={};
                if(damage==0)o.transform[15]=0;
                if(damage==1)o.transform[3]=.25f;
                if(damage==2){o.generation[3]=1;o.planes[12]=1;o.planes[15]=2;}
                if(damage==3)o.generation[0]=2;
                if(damage==4){o.generation[2]=1;o.planes[8]=2;}
                write(dir.filePath("rejected.jt"),family,minor,o);const auto result=Jt::Importer::read(dir.filePath("rejected.jt"));
                const auto& mesh=*result.scene->mMeshes[0];
                check(!result.warnings.isEmpty()&&result.scene->mMaterials[mesh.mMaterialIndex]->GetTextureCount(aiTextureType_DIFFUSE)==0,"Unrepresentable homogeneous mapping was silently accepted");
                for(unsigned i=0;i<mesh.mNumVertices;++i)check(mesh.mVertices[i]==source.mVertices[i]&&mesh.mTextureCoords[0][i]==source.mTextureCoords[0][i],"Rejected homogeneous mapping leaked partial UVs");
            }
            if(family==10){
                auto first=o,second=o;first.generation={};second.generation={};first.planes={};second.planes={};
                first.transform=TextureFixture::Options{}.transform;second.transform=first.transform;
                first.transform[15]=2;second.transform[15]=4;first.wrapped=second.wrapped=false;
                TextureFixture::writePair(dir.filePath("shared.jt"),minor,first,second);
                const auto shared=Jt::Importer::read(dir.filePath("shared.jt"));
                check(shared.scene->mNumMeshes==2&&shared.scene->mNumTextures==1,"Homogeneous appearance sharing collapsed mappings or duplicated pixels");
                expected(*shared.scene->mMeshes[0],source,0);
                for(unsigned i=0;i<source.mNumVertices;++i){
                    const auto& a=shared.scene->mMeshes[1]->mTextureCoords[0][i];const auto& b=source.mTextureCoords[0][i];
                    check(near(a.x,b.x*.25f)&&near(a.y,b.y*.25f),"Second shared homogeneous map reused the first UVs");
                }
            }
        }
    }
    std::cout<<"Homogeneous texture face proof, R/Q generation, copy/export and fallback passed\n";
}
