// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original version-2/Sabot fixtures. No downloaded CAD geometry is included.
#include "JtContainerFixture.h"
#include "JtLegacyFixture.h"
#include "Jt/JtImporter.h"
#include <JtAttribute_Material.hxx>
#include <JtAttribute_GeometricTransform.hxx>
#include <JtAttribute_DrawStyle.hxx>
#include <JtNode_Partition.hxx>
#include <JtData_Limits.hxx>
#include <QTemporaryDir>
#include <QDir>
#include <assimp/material.h>
#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
#include <iostream>

namespace {
using Fixture::Bytes;
const QString sabot="96603dd3-5a0f-40f5-97cb-1e9647cbd37e";
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
template<class F> void reject(F action) {
    try {action();} catch(const Jt::Cancelled&) {throw;} catch(const std::exception&) {return;}
    throw std::runtime_error("Malformed modern attribute fixture accepted");
}
Bytes material(bool big,unsigned version=2,int palette=-1) {
    Bytes b(big);b.u8(version);b.u8(8);b.u32(0);b.u32(0x1ff);b.u8(1);b.u16(0x39a0);
    for(unsigned color=0;color<4;++color)for(unsigned c=0;c<4;++c)b.f32(c==3?1.f:color==1?float(c==1):0.f);
    b.f32(32);b.f32(0);b.f32(1);if(version==2)b.u32(quint32(palette));return b;
}
Bytes transform(bool big,unsigned version=2,int palette=-1) {
    Bytes b(big);b.u8(version);b.u8(8);b.u32(0);b.u32(0);b.u8(1);b.u16(8);b.f64(10);
    if(version==2)b.u32(quint32(palette));return b;
}
Bytes style(bool big,unsigned version=2,int palette=-1) {
    Bytes b(big);b.u8(version);b.u8(8);b.u32(0);b.u32(0x3f);b.u8(1);b.u8(0x0a);
    if(version==2)b.u32(quint32(palette));return b;
}
void attribute(Bytes& graph,const QString& type,unsigned id,const Bytes& body,bool wrapped) {
    if(!wrapped) {Fixture::element(graph,type,id,body,3);return;}
    Bytes embedded(graph.big);embedded.guid(type);embedded.u8(3);embedded.u32(id);embedded.append(body);
    // Repeated disposable outer IDs must not collide with actual attribute IDs.
    Fixture::element(graph,sabot,999,embedded,9);
}
Bytes graphWith(const Bytes& body,const QString& type=Fixture::type("10dd1030"),bool wrapped=false) {
    Bytes graph(body.big);Fixture::element(graph,Fixture::type("10dd103e"),1,Fixture::partition(body.big,{}, {}, {20}));
    attribute(graph,type,20,body,wrapped);Fixture::end(graph);Fixture::end(graph);graph.u16(1);graph.u32(0);return graph;
}
Handle(JtNode_Partition) read(const QString& path,const Bytes& graph) {
    Fixture::container(path,graph,3);
    const auto utf16=path.toStdU16String();
    Handle(JtData_Model) model=new JtData_Model(TCollection_ExtendedString(reinterpret_cast<const Standard_ExtCharacter*>(utf16.c_str())));
    const auto root=model->Init();check(!root.IsNull(),"Cannot decode modern attribute container");return root;
}
void writeAssembly(const QString& path,bool big,unsigned baseVersion=2) {
    Bytes graph(big);Fixture::element(graph,Fixture::type("10dd103e"),1,Fixture::partition(big,{}, {2},{20,21,22}));
    Fixture::element(graph,Fixture::type("10dd103e"),2,Fixture::partition(big,"geometry.jt",{}));
    attribute(graph,Fixture::type("10dd1030"),20,material(big,baseVersion),false);
    attribute(graph,Fixture::type("10dd1083"),21,transform(big,baseVersion),false);
    attribute(graph,Fixture::type("10dd1014"),22,style(big,baseVersion),false);
    Fixture::end(graph);Fixture::end(graph);graph.u16(1);graph.u32(0);Fixture::container(path,graph,3);
}
}
void modernAttributeTests() {
    QTemporaryDir tmp;check(tmp.isValid(),"Cannot create modern attribute test directory");const auto path=tmp.filePath("attributes.jt");
    for(bool big:{false,true})for(bool wrapped:{false,true})for(int pi:{-1,0,17}) {
        Bytes graph(big);Fixture::element(graph,Fixture::type("10dd103e"),1,Fixture::partition(big,{}, {}, {20,21,22}));
        attribute(graph,Fixture::type("10dd1030"),20,material(big,2,pi),wrapped);
        attribute(graph,Fixture::type("10dd1083"),21,transform(big,2,pi),wrapped);
        attribute(graph,Fixture::type("10dd1014"),22,style(big,2,pi),wrapped);
        Fixture::end(graph);Fixture::end(graph);graph.u16(1);graph.u32(0);
        const auto root=read(path,graph);check(root->Attributes().Count()==3,"Inner Sabot object identities were lost");
        const auto m=Handle(JtAttribute_Material)::DownCast(root->Attributes()[0]);
        const auto t=Handle(JtAttribute_GeometricTransform)::DownCast(root->Attributes()[1]);
        const auto d=Handle(JtAttribute_DrawStyle)::DownCast(root->Attributes()[2]);
        check(!m.IsNull()&&!t.IsNull()&&!d.IsNull(),"Sabot wrapper retained instead of attribute");
        check(m->PaletteIndex()==pi&&t->PaletteIndex()==pi&&d->PaletteIndex()==pi,"Trailing palette fields shifted");
        check(m->DiffuseColor()[1]==1&&m->Shininess()==32&&m->FieldFinalFlags()==0x1ff,"Material version-2 payload shifted");
        check(t->GetTrsf()[12]==10&&d->DataFlags()==0x0a,"Transform/style version-2 payload shifted");
        // Indexed entries are now rendered only through an explicit face-group map.
    }
    for(bool big:{false,true}) {
        const QString types[]={Fixture::type("10dd1030"),Fixture::type("10dd1083"),Fixture::type("10dd1014")};
        const Bytes bodies[]={material(big),transform(big),style(big)};
        for(unsigned k=0;k<3;++k)for(bool wrapped:{false,true}) {
            for(qsizetype n=0;n<bodies[k].data.size();++n) {
                Bytes shortBody(big);shortBody.data=bodies[k].data.left(n);
                reject([&]{read(path,graphWith(shortBody,types[k],wrapped));});
            }
            auto bad=bodies[k];bad.data[0]=3;reject([&]{read(path,graphWith(bad,types[k],wrapped));});
            bad=bodies[k];for(int j=1;j<=4;++j)bad.data[bad.data.size()-j]=char(0xff); // -1 is valid
            read(path,graphWith(bad,types[k],wrapped));
            bad=bodies[k];Bytes negative(big);negative.u32(quint32(-2));bad.data.chop(4);bad.data+=negative.data;
            reject([&]{read(path,graphWith(bad,types[k],wrapped));});
            if(wrapped) {bad=bodies[k];bad.u8(0);reject([&]{read(path,graphWith(bad,types[k],true));});}
        }
        // Wrapped headers cannot cross their enclosing envelope or recursively wrap.
        for(unsigned n=0;n<21;++n) {Bytes shortInner(big);shortInner.data=QByteArray(n,char(0));reject([&]{read(path,graphWith(shortInner,sabot,false));});}
        reject([&]{read(path,graphWith(material(big),sabot,true));});
        reject([&]{read(path,graphWith(material(big,1),Fixture::type("10dd1030"),true));});
        reject([&]{read(path,graphWith(material(big),Fixture::type("deadbeef"),true));});
        reject([&]{read(path,graphWith(Fixture::partition(big,{},{}),Fixture::type("10dd103e"),true));});
        reject([&]{read(path,graphWith(material(big),Fixture::type("10dd1106"),false));});
        // A duplicate inner ID is an actual graph collision, even if wrapper IDs differ.
        Bytes duplicate(big);Fixture::element(duplicate,Fixture::type("10dd103e"),1,Fixture::partition(big,{}, {}, {20}));
        attribute(duplicate,types[0],20,material(big),true);attribute(duplicate,types[0],20,material(big),true);
        Fixture::end(duplicate);Fixture::end(duplicate);duplicate.u16(1);duplicate.u32(0);reject([&]{read(path,duplicate);});
    }
    LegacyFixture::Options geometry;geometry.minorVersion=2;geometry.quantizedData=true;geometry.quantizedBits=4;
    LegacyFixture::write(tmp.filePath("geometry.jt"),geometry);
    for(bool big:{false,true})for(unsigned version:{1,2}) {
        writeAssembly(path,big,version);auto result=Jt::Importer::read(path);
        check(result.scene->mNumMeshes==1,"Modern fallback attributes changed mesh count");
        const auto* mesh=result.scene->mMeshes[0];aiColor4D c;
        check(mesh->mNumVertices==7&&mesh->mNumFaces==3,"Modern fallback attributes changed geometry");
        check(result.scene->mMaterials[mesh->mMaterialIndex]->Get(AI_MATKEY_COLOR_DIFFUSE,c)==AI_SUCCESS&&c.g==1&&c.r==0,"Modern fallback colour lost");
        std::function<void(const aiNode*,aiMatrix4x4)> walk=[&](const aiNode* node,aiMatrix4x4 world){
            world*=node->mTransformation;if(node->mNumMeshes)check(world.a4==20&&world.b4==-3,"Modern fallback transform composition lost");
            for(unsigned i=0;i<node->mNumChildren;++i)walk(node->mChildren[i],world);
        };walk(result.scene->mRootNode,aiMatrix4x4());
        Assimp::Exporter exporter;const auto glb=tmp.filePath("roundtrip.glb");
        check(exporter.Export(result.scene.get(),"glb2",glb.toStdString())==AI_SUCCESS,"Modern fallback GLB export failed");
        Assimp::Importer importer;const auto* imported=importer.ReadFile(glb.toStdString(),0);
        check(imported&&imported->mNumMeshes==1,"Modern fallback GLB roundtrip failed");
        const auto* exportedMesh=imported->mMeshes[0];aiColor4D exportedColour;
        check(exportedMesh->mNumFaces==3&&imported->mMaterials[exportedMesh->mMaterialIndex]->Get(AI_MATKEY_COLOR_DIFFUSE,exportedColour)==AI_SUCCESS&&exportedColour.g==1&&exportedColour.r==0,"GLB roundtrip lost fallback appearance/triangles");
        walk(imported->mRootNode,aiMatrix4x4());
    }
    {JtData_Limits::Scope scope([]{throw Jt::Cancelled();});bool stopped=false;
     try{read(path,graphWith(material(false)));}catch(const Jt::Cancelled&){stopped=true;}
     check(stopped,"Element parsing swallowed cancellation");}
    std::cout<<"Modern attribute v2 tails, Sabot identity/envelopes, fallback scene/GLB and malformed cases passed\n";
}
void modernAttributeFixtureFiles(const QString& folder) {
    check(QDir().mkpath(folder),"Cannot create modern attribute fixture folder");
    LegacyFixture::Options o;o.minorVersion=2;o.quantizedData=true;o.quantizedBits=4;LegacyFixture::write(QDir(folder).filePath("geometry.jt"),o);
    for(bool big:{false,true})writeAssembly(QDir(folder).filePath(big?"attribute_v2_be.jt":"attribute_v2_le.jt"),big);
    Fixture::container(QDir(folder).filePath("palette_not_enabled.jt"),graphWith(material(false,2,0),Fixture::type("10dd1030"),true),3);
}
