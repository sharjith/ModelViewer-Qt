// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original literal combiner expectations and per-unit inheritance/control graphs.
#include "JtTextureStackFixture.h"
#include "JtLegacyFixture.h"
#include "Jt/JtImporter.h"
#include "SceneUtils.h"
#include <QImage>
#include <QColor>
#include <QTemporaryDir>
#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
#include <iostream>
#include <limits>
#include <string>
namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
TextureFixture::Options tex(int mode=2,int unit=0,unsigned format=2,const char* bytes="4080c080"){
    TextureFixture::Options o;o.coordinate=0;o.channel=unit;o.blendType=mode;o.pixelFormat=format;o.minFilter=o.magFilter=1;o.pixels=QByteArray("");
    for(int i=0;i<4;++i)o.pixels+=QByteArray::fromHex(bytes);o.total=o.mipBytes=int(o.pixels.size());return o;
}
const aiMaterial* material(const aiScene& scene){check(scene.mNumMeshes==1,"Batch changed mesh count");return scene.mMaterials[scene.mMeshes[0]->mMaterialIndex];}
QImage image(const aiScene& scene){aiString path;const auto* m=material(scene);
    if(m->GetTexture(aiTextureType_DIFFUSE,0,&path)!=AI_SUCCESS)check(m->GetTexture(aiTextureType_BASE_COLOR,0,&path)==AI_SUCCESS,"Batch image association missing");
    const auto* t=scene.GetEmbeddedTexture(path.C_Str());check(t,"Batch embedded image missing");return QImage::fromData(reinterpret_cast<const uchar*>(t->pcData),int(t->mWidth)).convertToFormat(QImage::Format_RGBA8888);}
const aiMetadata* units(const aiNode* node){
    if(node->mNumMeshes&&node->mMetaData)for(unsigned i=0;i<node->mMetaData->mNumProperties;++i)
        if(node->mMetaData->mKeys[i]==aiString("JTTextureUnits"))return static_cast<const aiMetadata*>(node->mMetaData->mValues[i].mData);
    for(unsigned i=0;i<node->mNumChildren;++i)if(auto found=units(node->mChildren[i]))return found;return nullptr;
}
bool unit(const aiMetadata& units,int id,int type,bool automatic){
    aiMetadata record;int32_t actual=-1,role=-1;bool assigned=false;
    return units.Get(std::to_string(id),record)&&record.Get("Unit",actual)&&record.Get("TextureType",role)&&record.Get("Automatic",assigned)&&actual==id&&role==type&&assigned==automatic;
}
}
void textureBatchTests(){
    QTemporaryDir dir;check(dir.isValid(),"Cannot create batched texture test folder");
    LegacyFixture::Options geometry;geometry.quantizedData=true;geometry.quantizedBits=4;geometry.uvBinding=1;geometry.materialAlpha=.5f;geometry.materialDataFlags=0x3990;
    LegacyFixture::write(dir.filePath("geometry.jt"),geometry);const auto baseline=Jt::Importer::read(dir.filePath("geometry.jt"));
    geometry.materialDataFlags=0x3980;LegacyFixture::write(dir.filePath("opaque.jt"),geometry);
    geometry.materialDataFlags=0x3990;geometry.materialColor={.25f,.5f,.75f};LegacyFixture::write(dir.filePath("tinted.jt"),geometry);
    using Node=TextureStackFixture::Node;
    const std::vector<Node> flat{{1,{}, {2},{20}}, {2,"geometry.jt",{}, {}}};
    for(unsigned family:{9u,10u})for(unsigned minor:family==9?std::vector<unsigned>{0,5}:std::vector<unsigned>{0,3,5,7,8,9,10})
    for(unsigned version:family==9?std::vector<unsigned>{1,2,3}:std::vector<unsigned>{1})for(bool big:{false,true}){
        const auto read=[&](std::vector<TextureFixture::Options> attrs,std::vector<Node> nodes=std::vector<Node>{}){
            if(nodes.empty())nodes=flat;TextureStackFixture::write(dir.filePath("batch.jt"),family,minor,big,version,nodes,std::move(attrs));return Jt::Importer::read(dir.filePath("batch.jt"));};
        const auto colour=[&](std::vector<TextureFixture::Options> attrs,QColor expected,std::vector<Node> nodes=std::vector<Node>{}){
            auto result=read(std::move(attrs),std::move(nodes));const auto pixels=image(*result.scene);check(pixels.pixelColor(0,0)==expected,"Literal batch combiner pixel differs");
            const auto& mesh=*result.scene->mMeshes[0];const auto& source=*baseline.scene->mMeshes[0];
            check(mesh.mNumVertices==source.mNumVertices&&mesh.mNumFaces==source.mNumFaces,"Batch composition changed topology counts");
            for(unsigned i=0;i<mesh.mNumVertices;++i)check(mesh.mVertices[i]==source.mVertices[i]&&mesh.mNormals[i]==source.mNormals[i]&&mesh.mTextureCoords[0][i]==source.mTextureCoords[0][i],"Batch composition changed source geometry/UVs");
            return result;
        };
        colour({tex(1)},QColor(45,93,226,128)); // Decal retains incoming half alpha.
        colour({tex(1)},QColor(45,93,226,255),{{1,{}, {2},{20}}, {2,"opaque.jt",{}, {}}}); // Decal still uses texture alpha for RGB when framebuffer blending is disabled.
        auto blend=tex(4);blend.blendColor={1,.25f,0,1};auto blendScene=colour({blend},QColor(64,66,183,64));
        colour({tex(5)},QColor(64,128,255,64)); // Add clamps RGB, multiplies texture alpha.
        colour({tex(3,0,3,"40")},QColor(64,64,64,128));
        colour({tex(3,0,4,"4080")},QColor(64,64,64,128));
        colour({tex(3,0,10,"80")},QColor(0,0,255,128));
        colour({tex(3,0,10,"80")},QColor(137,188,225,128),{{1,{}, {2},{20}}, {2,"tinted.jt",{}, {}}}); // Linear factors survive the sRGB display encoding.
        auto first=tex(2),second=tex(3,3,2,"ff400040");
        const std::vector<Node> ordered{{1,{}, {2},{21,20}}, {2,"geometry.jt",{}, {}}};
        colour({first,second},QColor(255,64,0,64),ordered); // Graph list order is reversed deliberately.
        second.blendType=5;colour({first,second},QColor(255,64,192,16),ordered);
        second.blendType=1;colour({first,second},QColor(137,30,169,64),ordered);
        // The upper unit is inherited independently. Replacing lower unit 0 must
        // neither wipe upper unit 3 nor reset its own final/inhibit state.
        auto lower=tex(3,0,2,"ff400040");second=tex(2,3);
        const std::vector<Node> inherited{{1,{}, {2},{20,21}}, {2,"geometry.jt",{}, {22}}};
        colour({first,second,lower},QColor(64,27,0,32),inherited);
        auto locked=tex(4);locked.blendColor={1,.25f,0,1};locked.finalFields=16;locked.stateFlags=9; // whole-attribute final works in 9.x too
        auto child=tex(3,0,2,"ff400040");
        colour({locked,child},QColor(64,66,183,64),{{1,{}, {2},{20}}, {2,"geometry.jt",{}, {21}}});
        child.stateFlags=10;colour({locked,child},QColor(255,64,0,64),{{1,{}, {2},{20}}, {2,"geometry.jt",{}, {21}}});
        child=tex(3,0,2,"ff400040");child.inhibit=16; // incoming image changes, combiner/colour stay inherited
        blend=tex(4);blend.blendColor={1,.25f,0,1};
        colour({blend,child},QColor(255,30,255,32),{{1,{}, {2},{20}}, {2,"geometry.jt",{}, {21}}});
        if(family==10){locked=tex(4);locked.blendColor={1,.25f,0,1};locked.finalFields=16;
            colour({locked,tex(3,0,2,"ff400040")},QColor(255,30,255,32),{{1,{}, {2},{20}}, {2,"geometry.jt",{}, {21}}});}
        for(unsigned damage=0;damage<5;++damage){auto bad=tex(2,1);
            if(damage==0)bad.minFilter=2;if(damage==1)bad.transform[12]=.25f;if(damage==2)bad.coordinate=2;
            if(damage==3){bad.width=1;bad.pixels=bad.pixels.left(8);bad.total=bad.mipBytes=8;}
            if(damage==4)bad.blendType=6;
            const auto rejected=read({first,bad},{{1,{}, {2},{20,21}}, {2,"geometry.jt",{}, {}}});
            check(!rejected.warnings.isEmpty()&&material(*rejected.scene)->GetTextureCount(aiTextureType_DIFFUSE)==0,"Incompatible texture stack silently produced partial appearance");
        }
        auto none=tex(0,3);const auto noOp=read({first,none},{{1,{}, {2},{20,21}}, {2,"geometry.jt",{}, {}}});
        check(image(*noOp.scene).pixelColor(0,0)==QColor(64,128,192,128),"None combiner erased lower unit");
        auto ignored=tex(3,3,2,"ff400040");ignored.stateFlags=12;
        const auto ignoredStage=read({first,ignored},{{1,{}, {2},{20,21}}, {2,"geometry.jt",{}, {}}});
        const auto* ignoredUnits=units(ignoredStage.scene->mRootNode);
        check(ignoredUnits&&ignoredUnits->mNumProperties==1&&image(*ignoredStage.scene).pixelColor(0,0)==QColor(64,128,192,128),"Ignored attribute changed unit accumulation");
        if(family==10||version==3){
            auto automatic=tex();automatic.channel=-1;automatic.textureType=4;
            auto explicit0=tex(3),explicit3=tex(0,3);
            const auto assigned=read({automatic,explicit0,explicit3},{{1,{}, {2},{20,21,22,20}}, {2,"geometry.jt",{}, {}}});
            const auto* effective=units(assigned.scene->mRootNode);check(effective&&effective->mNumProperties==3&&unit(*effective,0,9,false)&&unit(*effective,1,4,true)&&unit(*effective,3,9,false),"Automatic assignment displaced explicit units or duplicated inherited attribute");
            auto reset=tex();reset.textureType=26;reset.imageCount=0;
            auto sparse=tex(3,std::numeric_limits<int>::max(),2,"ff400040");
            colour({first,sparse},QColor(255,64,0,64),{{1,{}, {2},{20,21}}, {2,"geometry.jt",{}, {}}});
            auto invalid=automatic;invalid.channel=0;
            const auto invalidRole=read({explicit0,invalid},{{1,{}, {2},{20,21}}, {2,"geometry.jt",{}, {}}});
            check(!invalidRole.warnings.isEmpty()&&image(*invalidRole.scene).pixelColor(0,0)==QColor(64,128,192,128),"Invalid role/channel assignment displaced a valid explicit image");
            const auto cleared=read({first,reset},{{1,{}, {2},{20}}, {2,"geometry.jt",{}, {21}}});
            check(material(*cleared.scene)->GetTextureCount(aiTextureType_DIFFUSE)==0,"Reset left inherited colour units active");
            auto light=automatic;light.textureType=20;
            const auto retained=read({first,light,reset},{{1,{}, {2},{20,21}}, {2,"geometry.jt",{}, {22}}});
            effective=units(retained.scene->mRootNode);check(effective&&unit(*effective,1,20,true),"Reset discarded excepted light-map role state");
            locked=first;locked.stateFlags=9;
            const auto notForced=read({locked,reset},{{1,{}, {2},{20}}, {2,"geometry.jt",{}, {21}}});
            check(material(*notForced.scene)->GetTextureCount(aiTextureType_DIFFUSE)>0,"Ordinary reset discarded final inherited texture");
            reset.stateFlags=10;
            const auto forced=read({locked,reset},{{1,{}, {2},{20}}, {2,"geometry.jt",{}, {21}}});
            check(material(*forced.scene)->GetTextureCount(aiTextureType_DIFFUSE)==0,"Forced reset did not clear final inherited texture");
            reset.stateFlags=9;const auto fenced=read({reset,first},{{1,{}, {2},{20}}, {2,"geometry.jt",{}, {21}}});
            check(material(*fenced.scene)->GetTextureCount(aiTextureType_DIFFUSE)==0,"Final reset failed to fence a newly introduced descendant unit");
            auto forceImage=first;forceImage.stateFlags=10;
            const auto unfenced=read({reset,forceImage},{{1,{}, {2},{20}}, {2,"geometry.jt",{}, {21}}});
            check(material(*unfenced.scene)->GetTextureCount(aiTextureType_DIFFUSE)>0,"Forced image failed to override inherited final reset");
        }
        if(family==10&&minor==0&&!big){
            std::unique_ptr<aiScene> copy(SceneUtils::deepCopyScene(blendScene.scene.get()));check(copy&&image(*copy).pixelColor(0,0)==QColor(64,66,183,64),"Scene copy changed composed texture");
            aiColor4D incoming;check(material(*copy)->Get("$mat.jt.sourceDiffuse",0,0,incoming)==AI_SUCCESS&&incoming==aiColor4D(0,0,1,.5f),"Scene copy lost original material factors");
            const auto* copiedUnits=units(copy->mRootNode);check(copiedUnits&&unit(*copiedUnits,0,9,false),"Scene copy lost effective unit metadata");
            Assimp::Exporter exporter;const auto path=dir.filePath("batch.glb").toStdString();check(exporter.Export(blendScene.scene.get(),"glb2",path)==AI_SUCCESS,"Cannot export composed texture");
            Assimp::Importer importer;const auto* glb=importer.ReadFile(path,0);check(glb&&image(*glb).pixelColor(0,0)==QColor(64,66,183,64),"GLB changed composed texture");
        }
    }
    QImage external(2,2,QImage::Format_RGBA8888);external.fill(QColor(64,128,192,128));check(external.save(dir.filePath("original_texture.png")),"Cannot write external Replace image");
    auto outside=tex(3);outside.inlineImage=false;TextureFixture::write(dir.filePath("external.jt"),0,outside);
    check(image(*Jt::Importer::read(dir.filePath("external.jt")).scene).pixelColor(0,0)==QColor(64,128,192,128),"External RGBA Replace retained incoming alpha");
    std::vector<TextureFixture::Options> tooMany;std::vector<unsigned> attrs;
    for(unsigned i=0;i<65;++i){tooMany.push_back(tex(2,int(i)));attrs.push_back(20+i);}
    TextureStackFixture::write(dir.filePath("limit.jt"),10,0,false,1,{{1,{}, {2},attrs},{2,"geometry.jt",{}, {}}},tooMany);
    bool rejected=false;try{Jt::Importer::read(dir.filePath("limit.jt"));}catch(const std::exception& e){rejected=std::string(e.what()).find("texture unit limit")!=std::string::npos;}check(rejected,"Texture unit resource cap was not enforced");
    std::cout<<"Batched combiners, ordered units, accumulation/final/forced/reset, automatic state, rejection and copy/export checks passed\n";
}
