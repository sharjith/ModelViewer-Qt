// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original known-value role controls, not authentic exporter qualification.
#include "JtLegacyFixture.h"
#include "JtTextureStackFixture.h"
#include "JtTextureTangents.h"
#include "Jt/JtImporter.h"
#include "SceneUtils.h"
#include <QTemporaryDir>
#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <cmath>
#include <iostream>

namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void basis(){
    aiMesh mesh;mesh.mNumVertices=3;mesh.mVertices=new aiVector3D[3]{{0,0,0},{2,0,0},{0,3,0}};
    mesh.mNormals=new aiVector3D[3]{{0,0,1},{0,0,1},{0,0,1}};
    mesh.mTextureCoords[0]=new aiVector3D[3]{{0,0,0},{1,0,0},{0,1,0}};mesh.mNumUVComponents[0]=2;
    mesh.mNumFaces=1;mesh.mFaces=new aiFace[1];mesh.mFaces[0].mNumIndices=3;mesh.mFaces[0].mIndices=new unsigned[3]{0,1,2};
    Jt::TextureTangents::apply(mesh,[]{});
    for(unsigned i=0;i<3;++i)check(mesh.mTangents[i]==aiVector3D(1,0,0)&&mesh.mBitangents[i]==aiVector3D(0,1,0),"Known normal basis differs");
    auto* original=mesh.mTangents;
    for(unsigned damage=0;damage<3;++damage){
        mesh.mTextureCoords[0][2]={0,damage==0?-1.f:damage==1?0.f:1.f,0};bool rejected=false;
        try{Jt::TextureTangents::apply(mesh,[&]{if(damage==2)throw std::runtime_error("cancel probe");});}
        catch(const std::runtime_error&){rejected=true;}
        check(rejected&&mesh.mTangents==original&&mesh.mTangents[0]==aiVector3D(1,0,0),"Rejected tangent build partially changed mesh");
    }
}
}

void textureRoleTests(){
    basis();QTemporaryDir dir;check(dir.isValid(),"Cannot create role fixture directory");
    LegacyFixture::Options geometry;geometry.quantizedData=true;geometry.quantizedBits=4;geometry.uvBinding=1;geometry.normalBinding=0;
    LegacyFixture::write(dir.filePath("geometry.jt"),geometry);
    auto missing=geometry;missing.uvBinding=0;LegacyFixture::write(dir.filePath("missing.jt"),missing);
    for(unsigned family:{9u,10u})for(unsigned minor:(family==9?std::vector<unsigned>{0,5}:std::vector<unsigned>{0,3,5,7,8,9,10}))for(bool big:{false,true}){
        const unsigned version=family==9?(minor==0?2:3):1;
        const auto read=[&](std::vector<TextureFixture::Options> specs,bool noUV=false){
            std::vector<unsigned> ids;for(unsigned i=0;i<specs.size();++i)ids.push_back(20+i);
            TextureStackFixture::write(dir.filePath("roles.jt"),family,minor,big,version,
                {{1,{}, {2},ids},{2,noUV?"missing.jt":"geometry.jt",{}, {}}},specs);
            return Jt::Importer::read(dir.filePath("roles.jt"));
        };
        TextureFixture::Options normal;normal.coordinate=0;normal.textureType=4;normal.channel=-1;
        normal.pixels=QByteArray::fromHex("8080ff8080ff8080ff8080ff");
        auto result=read({normal});const auto& mesh=*result.scene->mMeshes[0];
        const auto* mat=result.scene->mMaterials[mesh.mMaterialIndex];
        check(mat->GetTextureCount(aiTextureType_NORMALS)==1&&mat->GetTextureCount(aiTextureType_DIFFUSE)==0&&mesh.HasTangentsAndBitangents(),"Normal role lost slot or tangent basis");
        for(unsigned i=0;i<mesh.mNumVertices;++i)check(std::abs(mesh.mTangents[i].x-1)<1e-5&&std::abs(mesh.mBitangents[i].y-1)<1e-5,"Imported original UV basis differs");
        std::unique_ptr<aiScene> copy(SceneUtils::deepCopyScene(result.scene.get()));
        check(copy->mMeshes[0]->HasTangentsAndBitangents()&&copy->mMaterials[copy->mMeshes[0]->mMaterialIndex]->GetTextureCount(aiTextureType_NORMALS)==1,"Copy lost normal role");
        for(unsigned damage=0;damage<4;++damage){
            auto bad=normal;if(damage==0)bad.transform[0]=2;if(damage==1)bad.pixelFormat=3;if(damage==2)bad.wrapS=1;
            if(damage==1){bad.total=bad.mipBytes=4;bad.pixels=QByteArray(4,char(128));}
            const auto rejected=read({bad},damage==3);const auto& m=*rejected.scene->mMeshes[0];
            check(!rejected.warnings.isEmpty()&&!m.HasTangentsAndBitangents()&&rejected.scene->mMaterials[m.mMaterialIndex]->GetTextureCount(aiTextureType_NORMALS)==0,"Unsupported normal map was silently applied");
        }
        auto base=normal;base.textureType=9;base.channel=0;
        const auto combined=read({normal,base});const auto& combinedMesh=*combined.scene->mMeshes[0];
        const auto* combinedMat=combined.scene->mMaterials[combinedMesh.mMaterialIndex];
        check(combinedMat->GetTextureCount(aiTextureType_NORMALS)==1&&combinedMat->GetTextureCount(aiTextureType_DIFFUSE)==1,"Explicit color displaced automatic normal map");
        if(family==10&&minor==0&&!big){
            Assimp::Exporter exporter;const auto path=dir.filePath("normal.glb").toStdString();
            check(exporter.Export(combined.scene.get(),"glb2",path)==AI_SUCCESS,"Normal role export failed");
            Assimp::Importer importer;const auto* loaded=importer.ReadFile(path,0);
            check(loaded&&loaded->mMeshes[0]->HasTangentsAndBitangents()&&loaded->mMaterials[loaded->mMeshes[0]->mMaterialIndex]->GetTextureCount(aiTextureType_NORMALS)==1,"Normal GLB lost role or tangent basis");
            for(unsigned i=0;i<loaded->mMeshes[0]->mNumVertices;++i)
                check(std::abs(loaded->mMeshes[0]->mTangents[i].x-1)<1e-5&&std::abs(loaded->mMeshes[0]->mBitangents[i].y-1)<1e-5,"Normal GLB changed tangent basis");
        }
        for(unsigned fmt:{1u,2u,11u,12u}){
            auto post=base;post.textureType=2;post.blendType=3;post.pixelFormat=fmt;
            if(fmt==2||fmt==12){post.total=post.mipBytes=16;post.pixels=QByteArray::fromHex("ff000080ff000080ff000080ff000080");}
            const auto unlit=read({post});const auto* material=unlit.scene->mMaterials[unlit.scene->mMeshes[0]->mMaterialIndex];int flag=0;
            check(material->Get("$mat.gltf.unlit",0,0,flag)==AI_SUCCESS&&flag==1&&material->GetTextureCount(aiTextureType_DIFFUSE)==1,"Post-lit complete replacement lost unlit contract");
            if(family==10&&minor==0&&!big){
                Assimp::Exporter exporter;const auto path=dir.filePath("post.glb").toStdString();
                check(exporter.Export(unlit.scene.get(),"glb2",path)==AI_SUCCESS,"Post-lit export failed");
                Assimp::Importer importer;const auto* loaded=importer.ReadFile(path,0);flag=0;
                check(loaded&&loaded->mMaterials[loaded->mMeshes[0]->mMaterialIndex]->Get("$mat.gltf.unlit",0,0,flag)==AI_SUCCESS&&flag==1,"Post-lit GLB lost unlit contract");
            }
        }
        auto decal=base;decal.textureType=2;decal.blendType=1;
        const auto unlit=read({decal});int flag=0;check(unlit.scene->mMaterials[unlit.scene->mMeshes[0]->mMaterialIndex]->Get("$mat.gltf.unlit",0,0,flag)==AI_SUCCESS&&flag==1,"RGB post-lit Decal was not unlit");
        auto unsupported=decal;unsupported.blendType=2;
        const auto rejected=read({unsupported});check(!rejected.warnings.isEmpty()&&rejected.scene->mMaterials[rejected.scene->mMeshes[0]->mMaterialIndex]->GetTextureCount(aiTextureType_DIFFUSE)==0,"Post-lit Modulate was approximated as unlit");
    }
    std::cout<<"Bounded normal basis and post-lit material role checks passed\n";
}
