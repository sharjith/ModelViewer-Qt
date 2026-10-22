// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original affine matrices, known UV expressions and shared-resource controls.
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
std::array<float,16> matrix(unsigned test){
    auto t=TextureFixture::Options{}.transform;
    switch(test){
    case 1:t[0]=2;t[5]=2;break; // repeat twice along each axis
    case 2:t[12]=0.25f;t[13]=0.125f;break;
    case 3:t[0]=-1;t[12]=1;break; // reflect S around 0.5
    case 4:t[0]=0;t[1]=1;t[4]=-1;t[5]=0;t[12]=1;break;
    case 5:t[4]=0.5f;break; // shear S by T
    }
    return t;
}
aiVector3D expected(unsigned test,const aiVector3D& uv){
    switch(test){
    case 1:return {2*uv.x,2*uv.y,0};
    case 2:return {uv.x+0.25f,uv.y+0.125f,0};
    case 3:return {1-uv.x,uv.y,0};
    case 4:return {1-uv.y,uv.x,0};
    case 5:return {uv.x+0.5f*uv.y,uv.y,0};
    default:return uv;
    }
}
void compare(const aiMesh& actual,const aiMesh& source,unsigned test){
    check(actual.mNumVertices==source.mNumVertices&&actual.mNumFaces==source.mNumFaces,"Texture transform changed geometry counts");
    for(unsigned i=0;i<actual.mNumVertices;++i){
        check(actual.mVertices[i]==source.mVertices[i]&&actual.mNormals[i]==source.mNormals[i],"Texture transform changed positions/normals");
        const auto uv=expected(test,source.mTextureCoords[0][i]);const auto mapped=actual.mTextureCoords[0][i];
        check(near(uv.x,mapped.x)&&near(uv.y,mapped.y),"Affine texture matrix/UV association mismatch");
    }
    for(unsigned i=0;i<actual.mNumFaces;++i)for(unsigned j=0;j<3;++j)
        check(actual.mFaces[i].mIndices[j]==source.mFaces[i].mIndices[j],"Texture transform changed topology");
}
}
void textureTransformTests(){
    QTemporaryDir dir;check(dir.isValid(),"Cannot create texture transform test directory");
    LegacyFixture::Options geometry;geometry.quantizedData=true;geometry.quantizedBits=4;geometry.uvBinding=1;
    LegacyFixture::write(dir.filePath("geometry.jt"),geometry);
    const auto reference=Jt::Importer::read(dir.filePath("geometry.jt"));const auto& source=*reference.scene->mMeshes[0];
    for(unsigned minor:{0u,3u,5u,7u,8u,9u,10u})for(bool big:{false,true}){
        TextureFixture::Options o;o.coordinate=0;o.big=big;o.baseVersion=minor?2:1;o.wrapped=minor!=0;
        for(unsigned test=0;test<6;++test){
            o.transform=matrix(test);TextureFixture::write(dir.filePath("mapped.jt"),minor,o);
            const auto result=Jt::Importer::read(dir.filePath("mapped.jt"));
            check(result.scene->mNumMeshes==1&&result.scene->mNumTextures==1,"Affine texture resource missing");
            const auto& mesh=*result.scene->mMeshes[0];compare(mesh,source,test);
            check(result.scene->mMaterials[mesh.mMaterialIndex]->GetTextureCount(aiTextureType_DIFFUSE)==1,"Affine texture was disabled");
            std::unique_ptr<aiScene> copy(SceneUtils::deepCopyScene(result.scene.get()));
            check(bool(copy),"Cannot copy affine texture scene");compare(*copy->mMeshes[0],source,test);
            if(minor==0&&!big){
                Assimp::Exporter exporter;const auto path=dir.filePath("mapped.glb").toStdString();
                check(exporter.Export(result.scene.get(),"glb2",path)==AI_SUCCESS,"Cannot export affine texture GLB");
                Assimp::Importer importer;const auto* loaded=importer.ReadFile(path,0);
                check(loaded&&loaded->mNumMeshes==1&&loaded->mMeshes[0]->HasTextureCoords(0),"Affine texture GLB lost mesh/UVs");
                for(unsigned i=0;i<mesh.mNumVertices;++i){const auto a=mesh.mTextureCoords[0][i],b=loaded->mMeshes[0]->mTextureCoords[0][i];
                    check(near(a.x,b.x)&&near(a.y,b.y),"Affine GLB round trip changed effective UVs");}
            }
        }
        // Two references reuse the same decoded geometry and pixels, but their
        // different effective UVs must prevent appearance mesh-cache aliasing.
        auto first=o,second=o;first.wrapped=second.wrapped=false;first.transform=matrix(0);second.transform=matrix(3);
        TextureFixture::writePair(dir.filePath("pair.jt"),minor,first,second);
        const auto pair=Jt::Importer::read(dir.filePath("pair.jt"));
        check(pair.scene->mNumMeshes==2&&pair.scene->mNumTextures==1,"Shared image/geometry transform variants collapsed or duplicated pixels");
        compare(*pair.scene->mMeshes[0],source,0);compare(*pair.scene->mMeshes[1],source,3);
        // Projective and non-2D matrices retain geometry and a diagnostic.
        for(unsigned field:{3u,7u,14u}){
            o.transform=matrix(0);o.transform[field]=0.5f;TextureFixture::write(dir.filePath("unsupported.jt"),minor,o);
            const auto fallback=Jt::Importer::read(dir.filePath("unsupported.jt"));
            check(fallback.scene->mNumMeshes==1&&!fallback.warnings.isEmpty(),"Unsupported texture matrix lost geometry/diagnostic");
            compare(*fallback.scene->mMeshes[0],source,0);
            check(fallback.scene->mMaterials[fallback.scene->mMeshes[0]->mMaterialIndex]->GetTextureCount(aiTextureType_DIFFUSE)==0,"Unsupported texture matrix applied silently");
        }
        o.transform=matrix(0);o.transform[0]=std::numeric_limits<float>::max();
        TextureFixture::write(dir.filePath("overflow.jt"),minor,o);
        const auto overflow=Jt::Importer::read(dir.filePath("overflow.jt"));
        check(overflow.scene->mNumMeshes==1&&!overflow.warnings.isEmpty(),"Unrenderable finite matrix lost geometry/diagnostic");
        compare(*overflow.scene->mMeshes[0],source,0);
        check(overflow.scene->mMaterials[overflow.scene->mMeshes[0]->mMaterialIndex]->GetTextureCount(aiTextureType_DIFFUSE)==0,"Overflowing UVs retained an active texture");
    }
    std::cout<<"Texture affine UVs, endian/wrapped families, sharing, copy/export and unsupported-matrix fallback passed\n";
}
