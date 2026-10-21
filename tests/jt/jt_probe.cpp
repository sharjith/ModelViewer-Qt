#include "Jt/JtImporter.h"
#include <JtData_Limits.hxx>
#include <JtData_Model.hxx>
#include <JtNode_Group.hxx>
#include <JtNode_Partition.hxx>
#include <JtNode_Instance.hxx>
#include <JtAttribute_Material.hxx>
#include <set>
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <assimp/Exporter.hpp>
#include <assimp/material.h>
#include <iostream>
#include <cmath>
#include <limits>
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
    const QCommandLineOption publishedMaterials("published-material-order","Disable the narrow Solid Edge duplicate-material compatibility profile");
    const QCommandLineOption sourceAttributes("source-attributes","Print unaccumulated attributes in the input file graph; external partitions are not followed");
    const QCommandLineOption validate("validate","Check geometry, stored-normal lengths and node transforms; print local mesh bounds");
    const QCommandLineOption budget("allocation-budget-mib","Override cumulative charged-allocation budget for native diagnostics","MiB");
    parser.addOption(budget);parser.addOption(sourceAttributes);parser.addOption(publishedMaterials);
    parser.addOption(eye);parser.addOption(properties);parser.addOption(materials);parser.addOption(validate);parser.process(app);
    const auto arguments=parser.positionalArguments();
    if(arguments.size()<1 || arguments.size()>2) parser.showHelp(2);
    try {
        Jt::Options options;options.solidEdgeDuplicateMaterialCompatibility=!parser.isSet(publishedMaterials);
        if(parser.isSet(budget)){bool ok=false;const auto mib=parser.value(budget).toULongLong(&ok);if(!ok||!mib||mib>std::numeric_limits<size_t>::max()/(1024*1024))throw std::runtime_error("Invalid allocation budget");options.decodedAllocationBudget=size_t(mib)*1024*1024;}
        size_t charged=0;options.progress=[&](const QString&,int,int){charged=JtData_Limits::allocated;};
        if(parser.isSet(eye)) {
            const auto coordinates=parser.value(eye).split(',');
            if(coordinates.size()!=3) throw std::runtime_error("--eye requires x,y,z");
            std::array<double,3> point{};
            for(int i=0;i<3;++i) {bool ok=false;point[i]=coordinates[i].toDouble(&ok);if(!ok) throw std::runtime_error("Invalid --eye coordinate");}
            options.lodEyePoint=point;
        }
        if(parser.isSet(sourceAttributes)) {
            if(arguments.size()!=1) throw std::runtime_error("--source-attributes does not export geometry");
            JtData_Limits::Scope limits([&]{options.check();},options.decodedAllocationBudget);
            const auto path=arguments[0].toStdU16String();
            Handle(JtData_Model) model=new JtData_Model(TCollection_ExtendedString(reinterpret_cast<const Standard_ExtCharacter*>(path.c_str())));
            const auto root=model->Init();if(root.IsNull())throw std::runtime_error("Cannot read JT source graph");
            QJsonArray nodes;std::set<const void*> seen;
            std::function<void(const Handle(JtData_Object)&,unsigned)> walk=[&](const Handle(JtData_Object)& object,unsigned depth) {
                if(object.IsNull() || depth>512)throw std::runtime_error("Invalid JT source graph reference/depth");
                if(!seen.insert(object.get()).second)return;
                if(seen.size()>1000000)throw std::runtime_error("JT source graph node limit");
                const auto node=Handle(JtNode_Base)::DownCast(object);if(node.IsNull())return;
                QJsonArray attrs;
                for(size_t i=0;i<node->Attributes().Count();++i) {
                    const auto a=Handle(JtAttribute_Base)::DownCast(node->Attributes()[i]);if(a.IsNull())continue;
                    QJsonObject row{{"listIndex",int(i)},{"type",a->DynamicType()->Name()},{"baseVersion",a->BaseVersion()},{"paletteIndex",a->PaletteIndex()},{"inhibit",double(a->FieldInhibitFlags())},{"final",double(a->FieldFinalFlags())},{"ignored",a->AccumulationIgnored()},{"forced",a->AccumulationForced()}};
                    const auto m=Handle(JtAttribute_Material)::DownCast(a);
                    if(!m.IsNull()){const auto c=m->DiffuseColor();row["diffuse"]=QJsonArray{c[0],c[1],c[2],c[3]};row["dataFlags"]=int(m->DataFlags());}
                    attrs.append(row);
                }
                nodes.append(QJsonObject{{"node",QString::fromUtf16(reinterpret_cast<const char16_t*>(node->Name().ToExtString()))},{"type",node->DynamicType()->Name()},{"depth",int(depth)},{"attributes",attrs}});
                const auto instance=Handle(JtNode_Instance)::DownCast(object);if(!instance.IsNull())walk(instance->Object(),depth+1);
                const auto group=Handle(JtNode_Group)::DownCast(object);if(!group.IsNull())for(size_t i=0;i<group->Children().Count();++i)walk(group->Children()[i],depth+1);
            };walk(root,0);
            std::cout<<QJsonDocument(QJsonObject{{"nodes",nodes},{"externalPartitionsFollowed",false}}).toJson(QJsonDocument::Compact).constData()<<'\n';return 0;
        }
        auto result=Jt::Importer::read(arguments[0],options);
        std::cout<<"charged allocation bytes="<<charged<<'\n';
        unsigned vertices=0,triangles=0;
        for(unsigned i=0;i<result.scene->mNumMeshes;++i) {vertices+=result.scene->mMeshes[i]->mNumVertices;triangles+=result.scene->mMeshes[i]->mNumFaces;}
        std::cout<<"meshes="<<result.scene->mNumMeshes<<" vertices="<<vertices<<" triangles="<<triangles<<" fallbackParts="<<result.fallbackParts<<'\n';
        if(parser.isSet(validate)) {
            double lo[3]={INFINITY,INFINITY,INFINITY},hi[3]={-INFINITY,-INFINITY,-INFINITY};
            unsigned uvMeshes=0,colourMeshes=0;
            for(unsigned i=0;i<result.scene->mNumMeshes;++i) {
                const auto& mesh=*result.scene->mMeshes[i];
                uvMeshes+=mesh.HasTextureCoords(0);colourMeshes+=mesh.HasVertexColors(0);
                if(!mesh.HasNormals() || mesh.mMaterialIndex>=result.scene->mNumMaterials) throw std::runtime_error("Invalid mesh normals/material");
                for(unsigned v=0;v<mesh.mNumVertices;++v) {
                    for(unsigned channel=0;channel<AI_MAX_NUMBER_OF_TEXTURECOORDS;++channel)if(mesh.HasTextureCoords(channel))for(unsigned k=0;k<3;++k)if(!std::isfinite(mesh.mTextureCoords[channel][v][k]))throw std::runtime_error("Nonfinite UV");
                    if(mesh.HasVertexColors(0))for(unsigned k=0;k<4;++k)if(!std::isfinite(mesh.mColors[0][v][k]) || mesh.mColors[0][v][k]<0 || mesh.mColors[0][v][k]>1)throw std::runtime_error("Invalid colour component");
                    if(!std::isfinite(mesh.mNormals[v].Length()) || std::abs(mesh.mNormals[v].Length()-1)>1e-4) throw std::runtime_error("Invalid normalized normal");
                    for(unsigned k=0;k<3;++k) {const double x=mesh.mVertices[v][k];if(!std::isfinite(x))throw std::runtime_error("Nonfinite vertex");lo[k]=std::min(lo[k],x);hi[k]=std::max(hi[k],x);}
                }
                for(unsigned f=0;f<mesh.mNumFaces;++f) {
                    if(mesh.mFaces[f].mNumIndices!=3)throw std::runtime_error("Non-triangle face");
                    for(unsigned k=0;k<3;++k)if(mesh.mFaces[f].mIndices[k]>=mesh.mNumVertices)throw std::runtime_error("Invalid face index");
                }
            }
            unsigned occurrences=0,nodes=0;
            std::function<void(const aiNode*)> visit=[&](const aiNode* node) {
                ++nodes;occurrences+=node->mNumMeshes;
                for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)if(!std::isfinite(node->mTransformation[r][c]))throw std::runtime_error("Nonfinite transform");
                for(unsigned i=0;i<node->mNumMeshes;++i)if(node->mMeshes[i]>=result.scene->mNumMeshes)throw std::runtime_error("Invalid node mesh reference");
                for(unsigned i=0;i<node->mNumChildren;++i)visit(node->mChildren[i]);
            };visit(result.scene->mRootNode);
            std::cout<<"validated nodes="<<nodes<<" occurrences="<<occurrences<<" local mesh bounds="<<lo[0]<<','<<lo[1]<<','<<lo[2]<<" to "<<hi[0]<<','<<hi[1]<<','<<hi[2]<<'\n';
            std::cout<<"attribute channels: colour meshes="<<colourMeshes<<" UV meshes="<<uvMeshes<<'\n';
        }
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
                aiColor4D colour;
                if(material.Get(AI_MATKEY_COLOR_DIFFUSE,colour)==AI_SUCCESS)entry["diffuse"]=QJsonArray{colour.r,colour.g,colour.b,colour.a};
                float scalar=0;
                if(material.Get(AI_MATKEY_OPACITY,scalar)==AI_SUCCESS)entry["opacity"]=scalar;
                if(material.Get(AI_MATKEY_SHININESS,scalar)==AI_SUCCESS) entry["shininess"]=scalar;
                if(material.Get(AI_MATKEY_ROUGHNESS_FACTOR,scalar)==AI_SUCCESS) entry["roughness"]=scalar;
                entries.append(entry);
            }
            std::cout<<QJsonDocument(QJsonObject{{"materials",entries}}).toJson(QJsonDocument::Compact).constData()<<'\n';
        }
        if(arguments.size()==2) {Assimp::Exporter exporter;if(exporter.Export(result.scene.get(),"glb2",arguments[1].toLocal8Bit().constData())!=AI_SUCCESS) throw std::runtime_error(exporter.GetErrorString());}
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
    return 0;
}
