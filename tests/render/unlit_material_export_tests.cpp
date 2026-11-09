// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#include "UnlitMaterialExport.h"
#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtEndian>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
QJsonObject document(const QString& path,bool binary){
    QFile file(path);check(file.open(QIODevice::ReadOnly),"Cannot read exported material control");
    auto bytes=file.readAll();
    if(binary){
        check(bytes.size()>=20&&bytes.left(4)=="glTF","Invalid GLB control header");
        const auto length=qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(bytes.constData()+12));
        check(length<=quint32(bytes.size()-20),"Invalid GLB JSON chunk size");bytes=bytes.mid(20,length);
    }
    const auto doc=QJsonDocument::fromJson(bytes);check(doc.isObject(),"Invalid exported JSON");return doc.object();
}
}
int main(int argc,char** argv){
    QCoreApplication application(argc,argv);
    try{
        QTemporaryDir directory;check(directory.isValid(),"Cannot create export controls");
        for(bool unlit:{false,true})for(bool binary:{false,true}){
            aiScene scene;scene.mRootNode=new aiNode;scene.mRootNode->mNumMeshes=1;scene.mRootNode->mMeshes=new unsigned[1]{0};
            scene.mNumMeshes=1;scene.mMeshes=new aiMesh*[1]{new aiMesh};auto& mesh=*scene.mMeshes[0];
            mesh.mNumVertices=3;mesh.mVertices=new aiVector3D[3]{{0,0,0},{1,0,0},{0,1,0}};
            mesh.mNormals=new aiVector3D[3]{{0,0,1},{0,0,1},{0,0,1}};mesh.mPrimitiveTypes=aiPrimitiveType_TRIANGLE;
            mesh.mNumFaces=1;mesh.mFaces=new aiFace[1];mesh.mFaces[0].mNumIndices=3;mesh.mFaces[0].mIndices=new unsigned[3]{0,1,2};
            scene.mNumMaterials=1;scene.mMaterials=new aiMaterial*[1]{new aiMaterial};auto& material=*scene.mMaterials[0];
            const aiColor4D base(.25f,.5f,.75f,.5f);const float opacity=.5f;
            material.AddProperty(&base,1,AI_MATKEY_BASE_COLOR);material.AddProperty(&base,1,AI_MATKEY_COLOR_DIFFUSE);
            material.AddProperty(&opacity,1,AI_MATKEY_OPACITY);const aiString alpha("BLEND");material.AddProperty(&alpha,"$mat.gltf.alphaMode",0,0);
            UnlitMaterialExport::apply(material,unlit);
            const auto path=directory.filePath(binary?"control.glb":"control.gltf");Assimp::Exporter exporter;
            check(exporter.Export(&scene,binary?"glb2":"gltf2",path.toStdString())==AI_SUCCESS,"Material export failed");
            const auto doc=document(path,binary);const auto output=doc["materials"].toArray().at(0).toObject();
            check(output["extensions"].toObject().contains("KHR_materials_unlit")==unlit,"Unlit extension lost or added to lit material");
            check(doc["extensionsUsed"].toArray().contains(QJsonValue("KHR_materials_unlit"))==unlit,"Unlit extension usage differs");
            check(output["alphaMode"].toString()=="BLEND","Unlit preservation changed alpha mode");
            const auto factors=output["pbrMetallicRoughness"].toObject()["baseColorFactor"].toArray();
            check(factors.size()==4&&factors[0].toDouble()==.25&&factors[1].toDouble()==.5&&factors[2].toDouble()==.75,"Unlit preservation changed color factors");
            Assimp::Importer importer;const auto* loaded=importer.ReadFile(path.toStdString(),0);check(loaded&&loaded->mNumMeshes==1,"Cannot reopen material control");
            int enabled=0;loaded->mMaterials[loaded->mMeshes[0]->mMaterialIndex]->Get("$mat.gltf.unlit",0,0,enabled);
            check((enabled!=0)==unlit,"Reopened material lost unlit contract");
        }
        std::cout<<"Unlit/lit GLB and glTF material transport passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
