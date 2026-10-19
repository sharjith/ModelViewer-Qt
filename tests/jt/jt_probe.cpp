#include "Jt/JtImporter.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <assimp/Exporter.hpp>
#include <assimp/material.h>
#include <iostream>
namespace {
QJsonObject metadataJson(const aiMetadata& data) {
    QJsonObject output;
    for(unsigned i=0;i<data.mNumProperties;++i) {
        const auto& entry=data.mValues[i];const auto key=QString::fromUtf8(data.mKeys[i].data,int(data.mKeys[i].length));
        switch(entry.mType) {
        case AI_AISTRING: {const auto& s=*static_cast<const aiString*>(entry.mData);output[key]=QString::fromUtf8(s.data,int(s.length));break;}
        case AI_INT32: output[key]=*static_cast<const int32_t*>(entry.mData);break;
        case AI_UINT64: output[key]=double(*static_cast<const uint64_t*>(entry.mData));break;
        case AI_FLOAT: output[key]=double(*static_cast<const float*>(entry.mData));break;
        case AI_AIMETADATA: output[key]=metadataJson(*static_cast<const aiMetadata*>(entry.mData));break;
        default: break;
        }
    }
    return output;
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    QCommandLineParser parser;parser.addHelpOption();
    parser.addPositionalArgument("input","JT file to inspect");
    parser.addPositionalArgument("output","Optional GLB export","[output.glb]");
    const QCommandLineOption eye("eye","Import-time RangeLOD eye position in JT world coordinates / source units","x,y,z");
    const QCommandLineOption properties("properties","Print supported JT node metadata as JSON");
    const QCommandLineOption materials("materials","Print resolved JT material/style fields as JSON");
    parser.addOption(eye);parser.addOption(properties);parser.addOption(materials);parser.process(app);
    const auto arguments=parser.positionalArguments();
    if(arguments.size()<1 || arguments.size()>2) parser.showHelp(2);
    try {
        Jt::Options options;
        if(parser.isSet(eye)) {
            const auto coordinates=parser.value(eye).split(',');
            if(coordinates.size()!=3) throw std::runtime_error("--eye requires x,y,z");
            std::array<double,3> point{};
            for(int i=0;i<3;++i) {bool ok=false;point[i]=coordinates[i].toDouble(&ok);if(!ok) throw std::runtime_error("Invalid --eye coordinate");}
            options.lodEyePoint=point;
        }
        auto result=Jt::Importer::read(arguments[0],options);
        unsigned vertices=0,triangles=0;
        for(unsigned i=0;i<result.scene->mNumMeshes;++i) {vertices+=result.scene->mMeshes[i]->mNumVertices;triangles+=result.scene->mMeshes[i]->mNumFaces;}
        std::cout<<"meshes="<<result.scene->mNumMeshes<<" vertices="<<vertices<<" triangles="<<triangles<<" fallbackParts="<<result.fallbackParts<<'\n';
        for(const auto& warning:result.warnings) std::cout<<warning.toStdString()<<'\n';
        QJsonArray propertyNodes;
        std::function<void(const aiNode*)> selections=[&](const aiNode* node) {
            int32_t selected=-1;double distance=0;
            if(node->mMetaData && node->mMetaData->Get("JTSelectedChild",selected)) {
                std::cout<<"selection="<<node->mName.C_Str()<<" child="<<selected;
                if(node->mMetaData->Get("JTRangeLODDistance",distance)) std::cout<<" distance="<<distance;
                std::cout<<'\n';
            }
            if(parser.isSet(properties) && node->mMetaData && node->mMetaData->HasKey("JTProperties")) {
                const auto data=metadataJson(*node->mMetaData);
                propertyNodes.append(QJsonObject{{"node",QString::fromUtf8(node->mName.C_Str())},{"JTProperties",data["JTProperties"]}});
            }
            for(unsigned i=0;i<node->mNumChildren;++i) selections(node->mChildren[i]);
        };selections(result.scene->mRootNode);
        if(parser.isSet(properties)) std::cout<<QJsonDocument(propertyNodes).toJson(QJsonDocument::Compact).constData()<<'\n';
        if(parser.isSet(materials)) {
            QJsonArray entries;
            for(unsigned i=0;i<result.scene->mNumMaterials;++i) {
                const auto& material=*result.scene->mMaterials[i];QJsonObject entry{{"index",int(i)}};int value=0;
                if(material.Get("$mat.jt.drawStyleFlags",0,0,value)==AI_SUCCESS) entry["drawStyleFlags"]=value;
                if(material.Get("$mat.jt.drawStyleFields",0,0,value)==AI_SUCCESS) entry["drawStyleFields"]=value;
                if(material.Get(AI_MATKEY_TWOSIDED,value)==AI_SUCCESS) entry["doubleSided"]=bool(value);
                if(material.Get(AI_MATKEY_ENABLE_WIREFRAME,value)==AI_SUCCESS) entry["wireframe"]=bool(value);
                if(material.Get("$mat.gltf.unlit",0,0,value)==AI_SUCCESS) entry["unlit"]=bool(value);
                if(material.Get(AI_MATKEY_SHADING_MODEL,value)==AI_SUCCESS) entry["shadingModel"]=value;
                entries.append(entry);
            }
            std::cout<<QJsonDocument(QJsonObject{{"materials",entries}}).toJson(QJsonDocument::Compact).constData()<<'\n';
        }
        if(arguments.size()==2) {Assimp::Exporter exporter;if(exporter.Export(result.scene.get(),"glb2",arguments[1].toLocal8Bit().constData())!=AI_SUCCESS) throw std::runtime_error(exporter.GetErrorString());}
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
    return 0;
}
