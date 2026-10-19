// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original synthetic scene fixtures; no vendor CAD data or XT decoder is used.
#include "JtSceneBuilder.h"
#include "JtContainerFixture.h"
#include "SceneUtils.h"
#include <QDir>
#include <QFile>
#include <JtNode_Group.hxx>
#include <JtNode_Instance.hxx>
#include <JtNode_LOD.hxx>
#include <JtNode_RangeLOD.hxx>
#include <JtNode_Switch.hxx>
#include <JtNode_Part.hxx>
#include <JtNode_Shape_Base.hxx>
#include <JtElement_ShapeLOD_Vertex.hxx>
#include <JtAttribute_Material.hxx>
#include <JtAttribute_GeometricTransform.hxx>
#include <JtProperty_String.hxx>
#include <JtProperty_Scalar.hxx>
#include <QTemporaryDir>
#include <assimp/material.h>
#include <cmath>
#include <iostream>
#include <limits>

namespace {
using Object=Handle(JtData_Object);
void check(bool value,const char* why) {if(!value) throw std::runtime_error(why);}
template<class F> void rejected(F action,const char* message) {
    try {action();} catch(const std::runtime_error& e) {check(std::string(e.what()).find(message)!=std::string::npos,"Unexpected scene failure diagnostic");return;}
    throw std::runtime_error("Expected scene failure");
}
template<class T> struct Node:T {
    void attributes(std::initializer_list<Object> values) {this->myAttributes.Allocate(values.size());size_t i=0;for(const auto& v:values)this->myAttributes[i++]=v;}
    void children(std::initializer_list<Object> values) {this->myChildren.Allocate(values.size());size_t i=0;for(const auto& v:values)this->myChildren[i++]=v;}
    void target(const Object& object) {this->myObject=object;}
    void hidden() {this->myIgnored=true;}
    void select(unsigned index) {this->mySelectedChild=index;}
    void ranges(std::initializer_list<float> values) {this->myVecR.Allocate(values.size());size_t i=0;for(float v:values)this->myVecR[i++]=v;this->myCenter={0,0,0};}
    void center(float x,float y,float z) {this->myCenter={x,y,z};}
    void late(std::initializer_list<Handle(JtProperty_LateLoaded)> values) {this->myLateLoads.Allocate(values.size());size_t i=0;for(const auto& v:values)this->myLateLoads[i++]=v;}
};
template<class T> opencascade::handle<Node<T>> node(const char* name) {
    opencascade::handle<Node<T>> n=new Node<T>;n->BindName(TCollection_ExtendedString(name));return n;
}
struct Triangle:JtElement_ShapeLOD_Vertex {
    Triangle() {
        myVertices.Allocate(3,3);myNormals.Allocate(3,3);myIndices.Allocate(3);
        for(int i=0;i<3;++i) {myIndices[i]=i;for(int j=0;j<3;++j){myVertices[i][j]=float(i==j+1);myNormals[i][j]=float(j==2);}}
    }
};
struct Property:JtProperty_LateLoaded {Property(bool valid=true,int type=7) {mySegmentType=type;if(valid)myDefferedObject=new Triangle;}};
auto leaf(const char* name="triangle") {
    auto n=node<JtNode_Shape_Base>(name);n->late({new Property});return n;
}
struct Material:JtAttribute_Material {
    Material(aiColor4D color,unsigned inhibit=0,unsigned finals=0,unsigned state=8,bool blending=false) {
        myStateFlags=state;myFieldInhibitFlags=inhibit;myFieldFinalFlags=finals;myDataFlags=blending?0x3990:0x3980;
        myDiffuseColor[0]=color.r;myDiffuseColor[1]=color.g;myDiffuseColor[2]=color.b;myDiffuseColor[3]=color.a;
        for(int i=0;i<4;++i) {myAmbientColor[i]=i==3?1.0f:0.2f;mySpecularColor[i]=i==3?1.0f:0.4f;myEmissionColor[i]=i==3?1.0f:0.0f;}
        myShininess=15;myReflectivity=0.2f;
    }
};
struct Transform:JtAttribute_GeometricTransform {
    Transform(double x,double y=0,unsigned finals=0,unsigned state=8) {
        myStateFlags=state;myFieldInhibitFlags=0;myFieldFinalFlags=finals;
        for(int i=0;i<16;++i) myTransform[i]=i%5==0?1:0;myTransform[12]=x;myTransform[13]=y;
    }
    void rotateScale() {myTransform[0]=0;myTransform[1]=2;myTransform[4]=-3;myTransform[5]=0;}
};
unsigned references(const aiNode& n) {unsigned count=n.mNumMeshes;for(unsigned i=0;i<n.mNumChildren;++i)count+=references(*n.mChildren[i]);return count;}
aiColor4D color(const Jt::ImportResult& result,const aiNode& n) {
    check(n.mNumMeshes==1,"Expected one mesh reference");aiColor4D c;
    check(result.scene->mMaterials[result.scene->mMeshes[n.mMeshes[0]]->mMaterialIndex]->Get(AI_MATKEY_COLOR_DIFFUSE,c)==AI_SUCCESS,"Missing material");return c;
}
float opacity(const Jt::ImportResult& result,const aiNode& n) {
    float value=-1;result.scene->mMaterials[result.scene->mMeshes[n.mMeshes[0]]->mMaterialIndex]->Get(AI_MATKEY_OPACITY,value);return value;
}
Jt::ImportResult build(const Handle(JtNode_Partition)& root,const Jt::Options& options={}) {
    QTemporaryDir temp;check(temp.isValid(),"Cannot create fixture directory");return Jt::Detail::buildScene(root,temp.filePath("synthetic.jt"),options);
}
void inheritance() {
    constexpr unsigned rgb=1u<<6,alpha=1u<<7;
    for(unsigned flags:{0u,1u,2u,3u,4u}) {
        auto root=node<JtNode_Partition>("root");auto group=node<JtNode_Group>("parent");auto shape=leaf();
        root->children({group});group->children({shape});
        group->attributes({new Material({1,0,0,0.5f},0,flags==1 || flags==2 ? rgb:0,8,true)});
        shape->attributes({new Material({0,1,0,0.25f},flags==3 ? rgb|alpha:0,0,flags==4 ? 12:flags==2 ? 10:8,true)});
        auto result=build(root);auto& meshNode=*result.scene->mRootNode->mChildren[0]->mChildren[0];
        const auto c=color(result,meshNode);const bool red=flags==1 || flags==3 || flags==4;
        check(c.r==(red?1:0) && c.g==(red?0:1),"Inhibit/final/force/ignore material inheritance failed");
        check(opacity(result,meshNode)==(flags==3 || flags==4 ? 0.5f:0.25f),"Inherited alpha failed");
        aiColor3D ambient;const auto* material=result.scene->mMaterials[result.scene->mMeshes[meshNode.mMeshes[0]]->mMaterialIndex];
        check(material->Get(AI_MATKEY_COLOR_AMBIENT,ambient)==AI_SUCCESS && ambient.r==0.2f,"Ambient attribute lost");
    }
    // Force overrides final once; it does not clear the final state for grandchildren.
    auto root=node<JtNode_Partition>("root");auto a=node<JtNode_Group>("a");auto b=node<JtNode_Group>("b");auto shape=leaf();
    root->children({a});a->children({b});b->children({shape});
    a->attributes({new Material({1,0,0,1},0,rgb)});b->attributes({new Material({0,1,0,1},0,0,10)});shape->attributes({new Material({0,0,1,1})});
    auto result=build(root);const auto c=color(result,*result.scene->mRootNode->mChildren[0]->mChildren[0]->mChildren[0]);
    check(c.g==1 && c.b==0,"Forced material cleared ancestor final state");
}
void instances() {
    auto root=node<JtNode_Partition>("root");auto shared=leaf("shared");
    auto a=node<JtNode_Instance>("a");auto b=node<JtNode_Instance>("b");auto c=node<JtNode_Instance>("c");
    a->target(shared);b->target(shared);c->target(shared);root->children({a,b,c});
    a->attributes({new Transform(10),new Material({1,0,0,1})});b->attributes({new Transform(20),new Material({1,0,0,1})});c->attributes({new Material({0,1,0,1})});
    auto result=build(root);auto& tree=*result.scene->mRootNode;
    check(result.scene->mNumMeshes==2 && references(tree)==3,"Shared geometry/material occurrence accounting failed");
    check(tree.mChildren[0]->mChildren[0]->mMeshes[0]==tree.mChildren[1]->mChildren[0]->mMeshes[0],"Identical instance appearances did not share a mesh");
    check(tree.mChildren[0]->mTransformation.a4==10 && tree.mChildren[1]->mTransformation.a4==20,"Instance placement lost");
    check(color(result,*tree.mChildren[2]->mChildren[0]).g==1,"Instance appearance variant lost");
    check(result.scene->mMeshes[0]->mNumVertices==3 && result.scene->mMeshes[1]->mNumVertices==3,"Appearance variant changed geometry");
}
void transforms() {
    for(unsigned mode:{0u,1u,2u}) {
        auto root=node<JtNode_Partition>("root");auto group=node<JtNode_Group>("placed");auto shape=leaf();root->children({group});group->children({shape});
        group->attributes({new Transform(10,0,mode?1:0)});shape->attributes({new Transform(0,5,0,mode==2?10:8)});
        auto result=build(root);const auto* placed=result.scene->mRootNode->mChildren[0];const auto world=placed->mTransformation*placed->mChildren[0]->mTransformation;
        check(world.a4==10 && world.b4==(mode==1?0:5),"Transform final/force accumulation failed");
    }
    auto root=node<JtNode_Partition>("root");auto group=node<JtNode_Group>("rotate");auto shape=leaf();root->children({group});group->children({shape});
    struct Rotation:Transform {Rotation():Transform(0){myTransform[0]=0;myTransform[1]=1;myTransform[4]=-1;myTransform[5]=0;}};
    group->attributes({new Rotation});shape->attributes({new Transform(2)});
    auto result=build(root);const auto* placed=result.scene->mRootNode->mChildren[0];const auto world=placed->mTransformation*placed->mChildren[0]->mTransformation;
    check(world.a4==0 && world.b4==2,"Nested rotation/translation multiplication order failed");
    auto scaledRoot=node<JtNode_Partition>("root");auto scaled=leaf();scaledRoot->children({scaled});
    struct Scale:Transform {Scale():Transform(0){myTransform[0]=-2;myTransform[5]=3;myTransform[10]=4;}};
    scaled->attributes({new Scale});auto scaledResult=build(scaledRoot);
    const auto& matrix=scaledResult.scene->mRootNode->mChildren[0]->mTransformation;
    check(matrix.a1==-2 && matrix.b2==3 && matrix.c3==4,"Negative/nonuniform scales were coerced");
}
void lodAndFailures() {
    auto root=node<JtNode_Partition>("root");auto lod=node<JtNode_LOD>("alternatives");auto broken=node<JtNode_Group>("broken");auto partial=leaf("partial");partial->hidden();auto invalid=node<JtNode_Shape_Base>("invalid");invalid->late({new Property(false)});
    root->children({lod});broken->children({partial,invalid});lod->children({broken,leaf("coarser")});
    auto result=build(root);check(result.scene->mNumMeshes==1 && references(*result.scene->mRootNode)==1 && result.storedMeshes==1,"Failed LOD leaked partial mesh resources");
    check(result.hiddenNodes.empty(),"Failed LOD left dangling hidden nodes");check(!result.warnings.empty(),"LOD fallback lacks a diagnostic");
    auto root2=node<JtNode_Partition>("root");auto variants=node<JtNode_Shape_Base>("alternatives");variants->late({new Property(false),new Property});root2->children({variants});
    auto result2=build(root2);check(result2.scene->mNumMeshes==1 && !result2.warnings.empty(),"Late-load alternative fallback failed");
    auto root3=node<JtNode_Partition>("root");auto empty=node<JtNode_LOD>("empty");empty->children({node<JtNode_Group>("empty child")});root3->children({empty});rejected([&]{build(root3);},"no usable JT LOD");
    auto root4=node<JtNode_Partition>("root");auto cycle=node<JtNode_Group>("cycle");cycle->children({cycle});root4->children({cycle});rejected([&]{build(root4);},"cycle/depth");cycle->children({});
    auto root5=node<JtNode_Partition>("root");auto bad=leaf();bad->attributes({new Material({1,1,1,std::numeric_limits<float>::quiet_NaN()})});root5->children({bad});rejected([&]{build(root5);},"alpha");
    auto singularRoot=node<JtNode_Partition>("root");auto singularLeaf=leaf();
    struct Singular:Transform {Singular():Transform(0){myTransform[0]=0;}};
    singularLeaf->attributes({new Singular});singularRoot->children({singularLeaf});rejected([&]{build(singularRoot);},"singular");
    auto brepRoot=node<JtNode_Partition>("root");auto brepOnly=node<JtNode_Part>("brep only");brepOnly->late({new Property(false,17)});brepOnly->children({node<JtNode_Group>("empty")});brepRoot->children({brepOnly});
    rejected([&]{build(brepRoot);},"B-Rep but no usable tessellation fallback");
    auto cancelRoot=node<JtNode_Partition>("root");auto cancelLod=node<JtNode_LOD>("cancelled LOD");cancelLod->children({leaf(),leaf()});cancelRoot->children({cancelLod});
    unsigned lodPolls=0;Jt::Options cancelLodOptions;cancelLodOptions.cancelled=[&]{return ++lodPolls>3;};rejected([&]{build(cancelRoot,cancelLodOptions);},"cancelled");
    auto root6=node<JtNode_Partition>("root");root6->children({leaf()});Jt::Options cancel;unsigned polls=0;cancel.cancelled=[&]{return ++polls>2;};rejected([&]{build(root6,cancel);},"cancelled");
}
}
void selectionTests() {
    auto root=node<JtNode_Partition>("root");auto selection=node<JtNode_Switch>("switch");
    auto first=leaf("first");auto second=leaf("second");
    // An unselected child must never be visited (it is deliberately cyclic).
    auto unused=node<JtNode_Group>("unused");unused->children({unused});
    selection->children({first,second,unused});selection->select(1);root->children({selection});
    auto result=build(root);auto chosen=result.scene->mRootNode->mChildren[0];
    check(chosen->mNumChildren==1 && chosen->mChildren[0]->mName==aiString("second"),"Switch selected the wrong child");
    int32_t selected=-2;check(chosen->mMetaData->Get("JTSelectedChild",selected) && selected==1,"Switch selection metadata missing");
    selection->select(0xffffffffu);root->children({selection,leaf("other")});
    result=build(root);check(result.scene->mRootNode->mChildren[0]->mNumChildren==0 && references(*result.scene->mRootNode)==1,"Switch none still displayed geometry");
    selection->select(3);rejected([&]{build(root);},"selected child is out of range");
    unused->children({}); // Release the deliberately cyclic test fixture.

    auto range=node<JtNode_RangeLOD>("range");auto third=leaf("third");range->children({first,second,third});range->ranges({10,20});root->children({range});
    // In-memory late loads have no file to reload after the builder unloads them.
    const auto resetGeometry=[&] {first->late({new Property});second->late({new Property});third->late({new Property});};
    const auto expect=[&](double distance,const char* name,int index) {
        resetGeometry();
        Jt::Options options;options.lodEyePoint=std::array<double,3>{distance,0,0};
        auto r=build(root,options);auto n=r.scene->mRootNode->mChildren[0];
        if(n->mNumChildren!=1 || n->mChildren[0]->mName!=aiString(name))
            throw std::runtime_error("RangeLOD distance "+std::to_string(distance)+" expected "+name+", got "+(n->mNumChildren ? n->mChildren[0]->mName.C_Str():"none"));
        int32_t child=-1;double d=-1;
        check(n->mMetaData->Get("JTSelectedChild",child) && child==index,"RangeLOD child metadata missing");
        check(n->mMetaData->Get("JTRangeLODDistance",d) && std::abs(d-std::abs(distance))<1e-6,"RangeLOD distance metadata wrong");
        check(references(*r.scene->mRootNode)==1,"RangeLOD emitted duplicate representations");
    };
    expect(0,"first",0);expect(10,"first",0);expect(10.01,"second",1);expect(20,"second",1);expect(21,"third",2);
    resetGeometry();result=build(root);check(result.scene->mRootNode->mChildren[0]->mChildren[0]->mName==aiString("first"),"Default RangeLOD policy changed");
    range->ranges({10});expect(11,"third",2); // Fewer limits than children is legal.
    range->ranges({1,2,3,4});expect(3.5,"third",2); // More limits than children is legal.
    range->ranges({});expect(100,"first",0);
    range->ranges({10,10});expect(10,"first",0);expect(11,"third",2);
    range->ranges({20,10});rejected([&]{build(root);},"range limits");
    range->ranges({-1});rejected([&]{build(root);},"range limits");
    range->ranges({std::numeric_limits<float>::quiet_NaN()});rejected([&]{build(root);},"range limits");
    range->ranges({10,20});range->center(std::numeric_limits<float>::infinity(),0,0);rejected([&]{build(root);},"centre");
    range->center(0,0,0);Jt::Options badEye;badEye.lodEyePoint=std::array<double,3>{NAN,0,0};rejected([&]{build(root,badEye);},"eye point");
    // Evaluate the centre using nested translation, rotation and nonuniform scale.
    auto parent=node<JtNode_Group>("translated");parent->attributes({new Transform(100)});parent->children({range});root->children({parent});
    auto transform=new Transform(10);transform->rotateScale();
    range->center(5,0,0);range->attributes({transform});
    Jt::Options eye;eye.lodEyePoint=std::array<double,3>{110,21,0};
    resetGeometry();result=build(root,eye);chosen=result.scene->mRootNode->mChildren[0]->mChildren[0];
    check(chosen->mChildren[0]->mName==aiString("second"),"RangeLOD ignored the accumulated centre transform");
    // A failed distance-selected representation falls back transactionally.
    auto broken=node<JtNode_Shape_Base>("broken");broken->late({new Property(false)});
    range->attributes({});range->center(0,0,0);range->children({first,broken,third});root->children({range});eye.lodEyePoint=std::array<double,3>{15,0,0};
    resetGeometry();result=build(root,eye);chosen=result.scene->mRootNode->mChildren[0];
    check(chosen->mChildren[0]->mName==aiString("first") && result.scene->mNumMeshes==1 && !result.warnings.isEmpty(),"RangeLOD fallback failed");
    selection->children({broken,first});selection->select(0);root->children({selection});
    rejected([&]{build(root);},"no supported JT display tessellation"); // Switches must not substitute an unauthored child.
}
void metadataTests() {
    QTemporaryDir temp;check(temp.isValid(),"Cannot create metadata fixture directory");const auto path=temp.filePath("metadata.jt");
    const auto read=[&] {
        Handle(JtData_Model) model=new JtData_Model(TCollection_ExtendedString(reinterpret_cast<const Standard_ExtCharacter*>(path.utf16())));
        auto source=model->Init();check(!source.IsNull(),"Cannot read metadata fixture");return source;
    };
    for(bool big:{false,true}) {
        Fixture::MetadataOptions options;options.big=big;Fixture::writeMetadata(path,{},options);auto source=read();
        check(source->Properties().size()==9 && source->Name()==TCollection_ExtendedString("Metadata assembly"),"Property table/name decoding failed");
        check(Handle(JtProperty_Integer)::DownCast(source->Properties()[2].value)->Value()==-7,"Signed integer property failed");
        check(Handle(JtProperty_Float)::DownCast(source->Properties()[3].value)->Value()==12.5f,"Float property failed");
        auto root=node<JtNode_Partition>("metadata root");auto shape=leaf();root->children({shape});
        for(const auto& p:source->Properties()) root->BindProperty(p.keyId,p.valueId,p.key,p.value);
        auto sourceChild=Handle(JtNode_Base)::DownCast(source->Children()[0]);
        for(const auto& p:sourceChild->Properties()) shape->BindProperty(p.keyId,p.valueId,p.key,p.value);
        auto result=build(root);aiMetadata records,childRecords;
        check(result.scene->mRootNode->mMetaData->Get("JTProperties",records) && records.mNumProperties==6,"Supported property records missing");
        const auto entry=[&](unsigned index) {aiMetadata record;check(records.Get(index,record),"Missing property record");return record;};
        aiString text;int32_t integer=0;float real=0;uint64_t flags=0;int32_t id=0;
        check(entry(0).Get("Value",text) && text==aiString("Metadata assembly;original-id"),"Raw name property was shortened");
        check(entry(1).Get("Value",text) && text==aiString("\xe9\xbd\xbf\xe8\xbd\xae\xe2\x9a\x99"),"Unicode property changed");
        check(entry(2).Get("Value",integer) && integer==-7 && entry(2).Get("ValueStateFlags",flags) && flags==0xa5,"Integer metadata type/flags failed");
        check(entry(2).Get("KeyAtomId",id) && id==104 && entry(2).Get("ValueAtomId",id) && id==105,"Property atom IDs changed");
        check(entry(3).Get("Value",real) && real==12.5f,"Float metadata type failed");
        check(entry(3).Get("KeyStateFlags",flags) && flags==0x45,"Key atom flags changed");
        aiString k1,k2,v1,v2;check(entry(4).Get("Key",k1) && entry(5).Get("Key",k2) && k1==k2 && entry(4).Get("Value",v1) && entry(5).Get("Value",v2) && v1!=v2,"Repeated property keys were overwritten");
        check(result.scene->mRootNode->mChildren[0]->mMetaData->Get("JTProperties",childRecords) && childRecords.mNumProperties==1,"Properties were inherited into a child");
        check(result.warnings.size()==2 && result.warnings[0].contains("1 unsupported") && result.warnings[1].contains("2 JT metadata"),"Unsupported/unrepresentable metadata was silent");
        auto copied=std::unique_ptr<aiScene>(SceneUtils::deepCopyScene(result.scene.get()));
        check(copied->mRootNode->mMetaData!=result.scene->mRootNode->mMetaData,"Copied scene aliases metadata");
        check(copied->mRootNode->mChildren[0]->mMetaData!=result.scene->mRootNode->mChildren[0]->mMetaData,"Copied child aliases metadata");
        result.scene.reset(); // Decoded source teardown must not invalidate the app's copy.
        aiMetadata copiedRecords;check(copied->mRootNode->mMetaData->Get("JTProperties",copiedRecords) && copiedRecords.mNumProperties==6,"Scene copy discarded nested metadata");
        aiMetadata copiedInteger;check(copiedRecords.Get(2,copiedInteger) && copiedInteger.Get("Value",integer) && integer==-7,"Copied typed property changed after source deletion");
        for(int failure=0;failure<6;++failure) {
            Fixture::MetadataOptions bad;bad.big=big;
            const char* diagnostic=nullptr;
            switch(failure) {
            case 0:bad.integerVersion=2;diagnostic="integer property version";break;
            case 1:bad.floatVersion=2;diagnostic="floating point property version";break;
            case 2:bad.stringVersion=3;diagnostic="string property version";break;
            case 3:bad.baseVersion=3;diagnostic="base property version";break;
            case 4:bad.tableVersion=2;diagnostic="property table version";break;
            default:bad.duplicateTable=true;diagnostic="Duplicate JT node property table";break;
            }
            Fixture::writeMetadata(path,{},bad);rejected(read,diagnostic);
        }
        options.baseVersion=2;options.stringVersion=2;Fixture::writeMetadata(path,{},options);
        source=read();check(source->Properties().size()==9 && source->Name()==TCollection_ExtendedString("Metadata assembly"),"Sample-qualified property version 2 failed");
    }
    auto bounded=node<JtNode_Partition>("bounded metadata");
    for(unsigned i=0;i<100000;++i) bounded->BindProperty(1,2,{},{});
    rejected([&]{bounded->BindProperty(1,2,{},{});},"node property count limit");
    bounded->children({leaf()});unsigned polls=0;Jt::Options cancelled;cancelled.cancelled=[&]{return ++polls>30;};
    rejected([&]{build(bounded,cancelled);},"cancelled");
}
void sceneTests() {inheritance();instances();transforms();lodAndFailures();selectionTests();metadataTests();std::cout<<"Synthetic JT assembly, attributes, transforms, selection and typed metadata passed\n";}

void containerTests(const QString& samples) {
    QTemporaryDir temp;check(temp.isValid(),"Cannot create container directory");
    const auto path=temp.filePath("assembly.jt");
    for(bool big:{false,true}) {
        Fixture::write(path,"missing.jt",big);
        Handle(JtData_Model) model=new JtData_Model(TCollection_ExtendedString(reinterpret_cast<const Standard_ExtCharacter*>(path.utf16())));
        const auto root=model->Init();check(!root.IsNull() && root->Children().Count()==2,"Synthetic container graph failed");
        const auto instance=Handle(JtNode_Instance)::DownCast(root->Children()[0]);
        check(!instance.IsNull() && instance->Name()==TCollection_ExtendedString("occurrence A"),"Container names/instances failed");
        rejected([&]{Jt::Importer::read(path);},"JT partition");
        Fixture::write(path,"missing.jt",big,true,true);
        rejected([&]{Jt::Importer::read(path);},"Duplicate JT object ID");
        for(bool range:{false,true}) {
            Fixture::writeSelection(path,"unselected_missing.jt","chosen.jt",range,1,big);
            Handle(JtData_Model) choiceModel=new JtData_Model(TCollection_ExtendedString(reinterpret_cast<const Standard_ExtCharacter*>(path.utf16())));
            auto choice=choiceModel->Init()->Children()[0];
            if(range) {
                auto decoded=Handle(JtNode_RangeLOD)::DownCast(choice);
                check(!decoded.IsNull() && decoded->RangeLimits().Count()==1 && decoded->RangeLimits()[0]==10,"RangeLOD container alignment failed");
            } else check(Handle(JtNode_Switch)::DownCast(choice)->SelectedChild()==1,"Switch container selection failed");
            Fixture::writeSelection(path,"a.jt","b.jt",range,0,big,2);
            rejected([&]{Jt::Importer::read(path);},range?"RangeLOD node version":"Switch node version");
        }
        Fixture::writeSelection(path,"a.jt","b.jt",false,2,big);
        rejected([&]{Jt::Importer::read(path);},"selected child is out of range");
        Fixture::writeSelection(path,"a.jt","b.jt",false,0xffffffffu,big);
        rejected([&]{Jt::Importer::read(path);},"no supported tessellation"); // Valid none, rather than an invalid index.
        Fixture::writeSelection(path,"a.jt","b.jt",true,0,big,1,{20,10});
        rejected([&]{Jt::Importer::read(path);},"range limits");
    }
    // Nested cycles must be rejected using canonical file identity.
    QDir(temp.path()).mkpath("parts");const auto bridge=temp.filePath("parts/bridge.jt");
    Fixture::write(path,"parts/bridge.jt");Fixture::write(bridge,"../assembly.jt",false,false);
    rejected([&]{Jt::Importer::read(path);},"JT partition");
    QTemporaryDir outside;Fixture::write(outside.filePath("outside.jt"),"",false,false);
    Fixture::write(path,outside.filePath("outside.jt"));rejected([&]{Jt::Importer::read(path);},"JT partition");
    if(!samples.isEmpty()) {
        check(QFile::copy(QDir(samples).filePath("Bolt.jt"),temp.filePath("parts/leaf.jt")),"Cannot copy local validation part");
        Fixture::write(bridge,"leaf.jt",false,false);
        for(bool big:{false,true}) {
            Fixture::write(path,"parts/bridge.jt",big);
            auto result=Jt::Importer::read(path);
            check(result.scene->mNumMeshes==2 && references(*result.scene->mRootNode)==2,"External assembly resources/occurrences mismatch");
            struct Placed {const aiNode* node;aiMatrix4x4 world;};std::vector<Placed> placed;
            std::function<void(const aiNode*,aiMatrix4x4)> walk=[&](const aiNode* n,aiMatrix4x4 world){world*=n->mTransformation;if(n->mNumMeshes)placed.push_back({n,world});for(unsigned i=0;i<n->mNumChildren;++i)walk(n->mChildren[i],world);};
            walk(result.scene->mRootNode,aiMatrix4x4());check(placed.size()==2,"External nested scene graph failed");
            check(placed[0].world.a4==10 && placed[1].world.a4==20,"External occurrence transforms failed");
            check(color(result,*placed[0].node).r==1 && color(result,*placed[1].node).g==1,"Ancestor final appearance across partitions failed");
            Fixture::writeSelection(path,"missing.jt","parts/leaf.jt",false,1,big);
            result=Jt::Importer::read(path);check(references(*result.scene->mRootNode)==1,"Switch traversed an unselected external file");
            Fixture::writeSelection(path,"missing.jt","parts/leaf.jt",true,0,big);
            result=Jt::Importer::read(path);check(references(*result.scene->mRootNode)==1 && !result.warnings.isEmpty(),"RangeLOD container fallback failed");
            Jt::Options far;far.lodEyePoint=std::array<double,3>{11,0,0};
            result=Jt::Importer::read(path,far);check(result.scene->mRootNode->mChildren[0]->mNumChildren==1,"RangeLOD container distance selection failed");
        }
    }
    std::cout<<"JT little/big-endian containers, names, external references and cycle checks passed"<<(samples.isEmpty()?"":" with local geometry")<<'\n';
}
