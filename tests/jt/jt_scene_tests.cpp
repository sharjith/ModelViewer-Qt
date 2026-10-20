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
#include <JtAttribute_DrawStyle.hxx>
#include <JtProperty_String.hxx>
#include <JtProperty_Scalar.hxx>
#include <QTemporaryDir>
#include <assimp/material.h>
#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
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
        myVersion=1;
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
struct DrawStyle:JtAttribute_DrawStyle {
    DrawStyle(unsigned flags,unsigned inhibit=0,unsigned finals=0,unsigned state=8) {
        myDataFlags=flags;myStateFlags=state;myFieldInhibitFlags=inhibit;myFieldFinalFlags=finals;
    }
};
struct BentPatch:JtElement_ShapeLOD_Vertex {
    BentPatch() {
        myVertices.Allocate(4,3);myNormals.Allocate(4,3);myIndices.Allocate(6);
        const float points[4][3]={{0,0,0},{1,0,0},{0,1,0},{0,0,1}};const int indices[6]={0,1,2,0,3,1};
        for(unsigned i=0;i<4;++i) for(unsigned j=0;j<3;++j) {myVertices[i][j]=points[i][j];myNormals[i][j]=j==0?0:float(1/std::sqrt(2.0));}
        for(unsigned i=0;i<6;++i) myIndices[i]=indices[i];
    }
};
struct PatchProperty:JtProperty_LateLoaded {PatchProperty() {mySegmentType=7;myDefferedObject=new BentPatch;}};
struct ColorPatch:JtElement_ShapeLOD_Vertex {
    using JtElement_ShapeLOD_Vertex::assembleVertexRecords;
    ColorPatch(bool withNormals=true,float alpha=1) {
        VertexData p(4,3),n(withNormals?2:0,3),c(2,4);IndicesVec vi(6),ai(6);
        const float points[4][3]={{0,0,0},{20,0,0},{20,20,0},{0,20,0}};
        const int indices[6]={0,1,2,0,2,3};
        for(int i=0;i<4;++i) for(int j=0;j<3;++j)p[i][j]=points[i][j];
        for(int i=0;i<2;++i) {for(int j=0;j<3;++j) {if(withNormals)n[i][j]=float(j==2);c[i][j]=float(j==i);}c[i][3]=alpha;}
        for(int i=0;i<6;++i) {vi[i]=indices[i];ai[i]=i/3;}
        assembleVertexRecords(p,n,c,vi,ai);
    }
};
struct ColorProperty:JtProperty_LateLoaded {
    ColorProperty(bool normals=true,float alpha=1) {mySegmentType=7;myDefferedObject=new ColorPatch(normals,alpha);}
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
void shininessTests() {
    struct Gloss:Material {
        Gloss(float exponent,unsigned inhibit=0,unsigned finals=0,unsigned state=8)
            :Material({0.6f,0.6f,0.6f,1},inhibit,finals,state) {myShininess=exponent;}
    };
    const auto resolved=[](const Jt::ImportResult& result,unsigned index) {
        float exponent=-1,roughness=-1;
        const auto& m=*result.scene->mMaterials[result.scene->mMeshes[index]->mMaterialIndex];
        check(m.Get(AI_MATKEY_SHININESS,exponent)==AI_SUCCESS,"JT shininess lost");
        check(m.Get(AI_MATKEY_ROUGHNESS_FACTOR,roughness)==AI_SUCCESS && std::abs(roughness-std::sqrt(2.0f/(exponent+2)))<1e-6f,"JT Phong roughness mapping failed");
        return exponent;
    };
    for(float value:{1.0f,4.0f,96.0f,128.0f}) {
        auto root=node<JtNode_Partition>("root");auto shape=leaf();root->children({shape});shape->attributes({new Gloss(value)});
        auto result=build(root);check(resolved(result,0)==value,"JT exponent changed");
        std::unique_ptr<aiScene> copy(SceneUtils::deepCopyScene(result.scene.get()));
        float restored=-1;check(copy->mMaterials[copy->mMeshes[0]->mMaterialIndex]->Get(AI_MATKEY_SHININESS,restored)==AI_SUCCESS && restored==value,"Scene copy lost shininess");
    }
    for(float value:{0.0f,129.0f,-1.0f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        auto root=node<JtNode_Partition>("root");auto shape=leaf();root->children({shape});shape->attributes({new Gloss(value)});
        rejected([&]{build(root);},std::isfinite(value)?"shininess is out of range":"nonfinite material");
    }
    // Table 16's bit 4 is vertex-colour override, not shininess. Even all
    // inhibit/final flags do not assign an undocumented lock to the exponent.
    for(unsigned mode:{0u,1u,2u,3u}) {
        auto root=node<JtNode_Partition>("root");auto group=node<JtNode_Group>("parent");auto shape=leaf();root->children({group});group->children({shape});
        group->attributes({new Gloss(4,0,0x1ff)});
        if(mode!=0) shape->attributes({new Gloss(mode==3?std::numeric_limits<float>::quiet_NaN():96,mode==1?0x1ff:0,0,mode==3?12:8)});
        auto result=build(root);check(resolved(result,0)==(mode==0 || mode==3?4:96),"Shininess replacement/ignored-state semantics failed");
    }
    auto root=node<JtNode_Partition>("root");auto shared=leaf();auto a=node<JtNode_Instance>("broad");auto b=node<JtNode_Instance>("tight");
    root->children({a,b});a->target(shared);b->target(shared);a->attributes({new Gloss(4)});b->attributes({new Gloss(96)});
    auto result=build(root);check(result.scene->mNumMeshes==2,"Distinct exponents collapsed in material/mesh cache");
    check(resolved(result,0)==4 && resolved(result,1)==96,"Instance shininess leaked");
    check(result.scene->mMeshes[0]->mNumVertices==3 && result.scene->mMeshes[1]->mNumFaces==1,"Shininess changed geometry");
    auto plainRoot=node<JtNode_Partition>("plain");plainRoot->children({leaf()});auto plain=build(plainRoot);
    float value=-1;check(plain.scene->mMaterials[plain.scene->mMeshes[0]->mMaterialIndex]->Get(AI_MATKEY_SHININESS,value)!=AI_SUCCESS,"Unauthored exponent was invented");
}
void vertexColorTests() {
    const auto colored=[](bool normals=true,float alpha=1) {
        auto n=node<JtNode_Shape_Base>("red-green seam");n->late({new ColorProperty(normals,alpha)});return n;
    };
    for(bool normals:{false,true}) for(bool flat:{false,true}) {
        auto root=node<JtNode_Partition>("colours");auto shape=colored(normals);root->children({shape});
        shape->attributes({new Material({0.2f,0.3f,0.4f,1}),new DrawStyle(flat?0x1a:0x0a)});
        auto result=build(root);const auto& mesh=*result.scene->mMeshes[0];
        check(mesh.mNumFaces==2 && mesh.mNumVertices==6 && mesh.HasVertexColors(0),"Colour seams/flat normals lost vertices");
        for(unsigned i=0;i<6;++i) {
            const auto c=mesh.mColors[0][i];check(c.r==float(i<3) && c.g==float(i>=3) && c.b==0 && c.a==1,"Colour corner remap failed");
            check(std::abs(mesh.mNormals[i].z-1)<1e-6f,"Colour binding broke stored/generated normals");
        }
        check(mesh.mVertices[0]==mesh.mVertices[3] && mesh.mColors[0][0]!=mesh.mColors[0][3],"Shared-position colour discontinuity lost");
        check(color(result,*result.scene->mRootNode->mChildren[0]).r==1,"Accumulated diffuse incorrectly tinted vertex colours");
        auto copy=std::unique_ptr<aiScene>(SceneUtils::deepCopyScene(result.scene.get()));
        check(copy->mMeshes[0]->HasVertexColors(0) && copy->mMeshes[0]->mColors[0][4].g==1,"Scene copy lost vertex colours");
        QTemporaryDir dir;const auto path=dir.filePath("colours.glb").toStdString();Assimp::Exporter exporter;
        check(exporter.Export(result.scene.get(),"glb2",path)==AI_SUCCESS,"Colour GLB export failed");
        Assimp::Importer reader;const auto scene=reader.ReadFile(path,0);
        check(scene && scene->mNumMeshes==1 && scene->mMeshes[0]->HasVertexColors(0) && scene->mMeshes[0]->mColors[0][4].g==1,"GLB roundtrip lost vertex colours");
    }
    struct Override:Material {
        Override(bool on,unsigned inhibit=0,unsigned finals=0,unsigned state=8):Material({0,0,1,1},inhibit,finals,state) {if(on)myDataFlags|=0x20;}
    };
    for(unsigned mode=0;mode<5;++mode) {
        auto root=node<JtNode_Partition>("root");auto group=node<JtNode_Group>("parent");auto shape=colored();root->children({group});group->children({shape});
        group->attributes({new Override(true,0,mode==0 || mode==2?1u<<4:0)});
        shape->attributes({new Override(false,mode==1?1u<<4:0,0,mode==2?10:mode==3?12:8)});
        auto result=build(root);check(result.scene->mMeshes[0]->HasVertexColors(0)==(mode==2 || mode==4),"Vertex-colour override final/inhibit/force/ignore failed");
    }
    // Material variants must not mutate cached canonical colour geometry.
    auto root=node<JtNode_Partition>("root");auto shared=colored();auto a=node<JtNode_Instance>("source colours");auto b=node<JtNode_Instance>("blue override");auto c=node<JtNode_Instance>("source colours again");
    root->children({a,b,c});a->target(shared);b->target(shared);c->target(shared);b->attributes({new Override(true)});
    auto result=build(root);check(result.scene->mNumMeshes==2 && result.scene->mMeshes[0]->HasVertexColors(0) && !result.scene->mMeshes[1]->HasVertexColors(0),"Override contaminated geometry cache");
    auto blendRoot=node<JtNode_Partition>("root");auto transparent=colored(true,0.5f);blendRoot->children({transparent});
    transparent->attributes({new Material({1,1,1,1},0,0,8,true)});
    rejected([&]{build(blendRoot);},"no supported JT display tessellation");
    transparent->late({new ColorProperty(true,0.5f)});transparent->attributes({new Material({1,1,1,1})});auto opaque=build(blendRoot);
    check(opaque.scene->mMeshes[0]->mColors[0][0].a==1,"Blending-disabled vertex alpha remained active");
    ColorPatch target;JtElement_ShapeLOD_Vertex::VertexData p(1,3),n(1,3),colors(1,3);JtElement_ShapeLOD_Vertex::IndicesVec vi(1),ai(1);
    vi[0]=0;ai[0]=1;rejected([&]{target.assembleVertexRecords(p,n,colors,vi,ai);},"index out of range");
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
void drawStyleTests() {
    for(unsigned field=0;field<6;++field) for(unsigned mode=0;mode<4;++mode) {
        auto root=node<JtNode_Partition>("root");auto shape=leaf();root->children({shape});
        root->attributes({new DrawStyle(0x0a,0,mode<=1?1u<<field:0)});
        shape->attributes({new DrawStyle(0x35,mode==2?1u<<field:0,0,mode==1?10:mode==3?12:8)});
        auto result=build(root);const auto material=result.scene->mMaterials[result.scene->mMeshes[0]->mMaterialIndex];
        int flags=-1;check(material->Get("$mat.jt.drawStyleFlags",0,0,flags)==AI_SUCCESS,"Draw style not retained");
        const unsigned dataBit=field==0?2u:field==1?1u:1u<<field;
        const unsigned expected=mode==3?0x0a:mode==0 || mode==2 ? (0x35&~dataBit)|(0x0a&dataBit):0x35;
        check(unsigned(flags)==expected,"Draw style inhibit/final/force/ignore field mapping failed");
        int twoSided=-1,unlit=-1,wire=-1,shading=-1;
        check(material->Get(AI_MATKEY_TWOSIDED,twoSided)==AI_SUCCESS && twoSided==int(!(expected&1)),"Back-face culling mapping failed");
        check(material->Get("$mat.gltf.unlit",0,0,unlit)==AI_SUCCESS && unlit==int(!(expected&8)),"Unlit mapping failed");
        check(material->Get(AI_MATKEY_ENABLE_WIREFRAME,wire)==AI_SUCCESS && wire==int(bool(expected&4)),"Wireframe hint missing");
        check(material->Get(AI_MATKEY_SHADING_MODEL,shading)==AI_SUCCESS && shading==int(unlit?aiShadingMode_Unlit:aiShadingMode_PBR_BRDF),"Flat style incorrectly disabled lighting");
        aiShadingMode exportShading=aiShadingMode_PBR_BRDF;
        check(material->Get(AI_MATKEY_SHADING_MODEL,exportShading)==AI_SUCCESS && int(exportShading)==shading,"Assimp exporter cannot read the shading enum");
    }
    // Same definition, different occurrence styles; exercise both cache orders.
    for(bool flatFirst:{false,true}) {
        auto root=node<JtNode_Partition>("root");auto shape=node<JtNode_Shape_Base>("bent patch");shape->late({new PatchProperty});
        auto flat=node<JtNode_Instance>("flat"),smooth=node<JtNode_Instance>("smooth"),same=node<JtNode_Instance>("same flat");
        flat->target(shape);smooth->target(shape);same->target(shape);
        flat->attributes({new DrawStyle(0x1a)});smooth->attributes({new DrawStyle(0x0a)});same->attributes({new DrawStyle(0x1a)});
        if(flatFirst) root->children({flat,smooth,same});else root->children({smooth,flat,same});
        auto result=build(root);check(result.scene->mNumMeshes==2 && references(*result.scene->mRootNode)==3,"Draw-style variants broke sharing");
        const auto flatNode=result.scene->mRootNode->mChildren[flatFirst?0:1]->mChildren[0];
        const auto smoothNode=result.scene->mRootNode->mChildren[flatFirst?1:0]->mChildren[0];
        const auto flatMesh=result.scene->mMeshes[flatNode->mMeshes[0]],smoothMesh=result.scene->mMeshes[smoothNode->mMeshes[0]];
        check(flatMesh->mNumVertices==6 && smoothMesh->mNumVertices==4 && flatMesh->mNumFaces==2,"Flat-normal geometry/cache corruption");
        check(result.scene->mRootNode->mChildren[2]->mChildren[0]->mMeshes[0]==flatNode->mMeshes[0],"Equal draw styles duplicated geometry");
        for(unsigned face=0;face<2;++face) for(unsigned corner=0;corner<3;++corner) {
            const auto index=flatMesh->mFaces[face].mIndices[corner];const auto n=flatMesh->mNormals[index];
            check(std::abs(n.x)<1e-6 && std::abs(n.y-float(face==1))<1e-6 && std::abs(n.z-float(face==0))<1e-6,"Flat normals do not match the face");
        }
        check(std::abs(smoothMesh->mNormals[0].y-float(1/std::sqrt(2.0)))<1e-6,"Flat variant changed canonical stored normals");
        const unsigned flatIndex=flatNode->mMeshes[0],smoothIndex=smoothNode->mMeshes[0];
        auto copied=std::unique_ptr<aiScene>(SceneUtils::deepCopyScene(result.scene.get()));result.scene.reset();
        check(copied->mMeshes[flatIndex]->mNumVertices==6 && copied->mMeshes[smoothIndex]->mNumVertices==4,"Application scene copy lost draw-style geometry");
        aiShadingMode shading=aiShadingMode_Unlit;int unlit=-1;
        const auto material=copied->mMaterials[copied->mMeshes[flatIndex]->mMaterialIndex];
        check(material->Get(AI_MATKEY_SHADING_MODEL,shading)==AI_SUCCESS && shading==aiShadingMode_PBR_BRDF && material->Get("$mat.gltf.unlit",0,0,unlit)==AI_SUCCESS && unlit==0,"Application scene copy lost lit flat style");
    }
    auto root=node<JtNode_Partition>("root");auto shape=leaf();root->children({shape});shape->attributes({new DrawStyle(0x0c)});
    auto result=build(root);check(result.warnings.size()==1 && result.warnings[0].contains("two-sided lighting"),"Unsupported lighting combination was not diagnosed");
    // A zero-area triangle must retain finite fallback normals.
    struct ZeroTriangle:Triangle {ZeroTriangle() {for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)myVertices[i][j]=0;}};
    struct ZeroProperty:JtProperty_LateLoaded {ZeroProperty(JtElement_ShapeLOD_Vertex* p) {mySegmentType=7;myDefferedObject=p;}};
    shape->late({new ZeroProperty(new ZeroTriangle)});shape->attributes({new DrawStyle(0x1a)});
    result=build(root);check(result.scene->mMeshes[0]->mNumFaces==1 && result.scene->mMeshes[0]->mNormals[0].z==1,"Degenerate flat triangle lost finite normals");
}
void sceneTests() {inheritance();shininessTests();vertexColorTests();instances();transforms();lodAndFailures();selectionTests();metadataTests();drawStyleTests();std::cout<<"Synthetic JT assembly, shininess, vertex colours, transforms, selection, metadata and draw styles passed\n";}

void containerTests(const QString& samples) {
    QTemporaryDir temp;check(temp.isValid(),"Cannot create container directory");
    const auto path=temp.filePath("assembly.jt");
    for(const char* version:{"6.4","7.0","8.3","9.5","10.1","10.2","10.4","10.6","10.10","10.11","11.0"}) {
        Fixture::write(path,"",false,false);
        QFile file(path);check(file.open(QIODevice::ReadOnly),"Cannot read version fixture");auto bytes=file.readAll();file.close();
        const auto header=(QByteArray("Version ")+version+" JT original unsupported-version fixture").leftJustified(80,' ');
        bytes.replace(0,80,header);check(file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size(),"Cannot write version fixture");file.close();
        const auto message=std::string("Unsupported JT version ")+version+"; native reader supports qualified JT 8.0-8.2, JT 10.0, JT 10.3 and JT 10.9 subsets";
        rejected([&]{Jt::Importer::read(path);},message.c_str());
    }
    for(bool big:{false,true}) for(unsigned minor:{0,3,9}) {
        Fixture::write(path,"missing.jt",big,true,false,false,minor);
        Handle(JtData_Model) model=new JtData_Model(TCollection_ExtendedString(reinterpret_cast<const Standard_ExtCharacter*>(path.utf16())));
        const auto root=model->Init();check(model->MinorVersion()==int(minor) && !root.IsNull() && root->Children().Count()==2,"Synthetic versioned container graph failed");
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
    for(bool big:{false,true}) {
        Fixture::writeDrawStyle(path,"missing.jt",0x1a,big);
        Handle(JtData_Model) model=new JtData_Model(TCollection_ExtendedString(reinterpret_cast<const Standard_ExtCharacter*>(path.utf16())));
        auto root=model->Init();check(!root.IsNull(),"Cannot read draw-style container");
        auto style=Handle(JtAttribute_DrawStyle)::DownCast(root->Attributes()[0]);
        check(!style.IsNull() && style->DataFlags()==0x1a && style->FieldFinalFlags()==0x3f,"Draw-style header alignment failed");
        Fixture::writeDrawStyle(path,"missing.jt",0x0a,big,2);rejected([&]{Jt::Importer::read(path);},"draw style version");
        Fixture::writeDrawStyle(path,"missing.jt",0x80,big);rejected([&]{Jt::Importer::read(path);},"reserved JT draw style flags");
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
        for(bool big:{false,true}) for(unsigned minor:{0,3,9}) {
            Fixture::write(path,"parts/bridge.jt",big,true,false,false,minor);
            auto result=Jt::Importer::read(path);
            check(result.scene->mNumMeshes==2 && references(*result.scene->mRootNode)==2,"External assembly resources/occurrences mismatch");
            struct Placed {const aiNode* node;aiMatrix4x4 world;};std::vector<Placed> placed;
            std::function<void(const aiNode*,aiMatrix4x4)> walk=[&](const aiNode* n,aiMatrix4x4 world){world*=n->mTransformation;if(n->mNumMeshes)placed.push_back({n,world});for(unsigned i=0;i<n->mNumChildren;++i)walk(n->mChildren[i],world);};
            walk(result.scene->mRootNode,aiMatrix4x4());check(placed.size()==2,"External nested scene graph failed");
            check(placed[0].world.a4==10 && placed[1].world.a4==20,"External occurrence transforms failed");
            check(color(result,*placed[0].node).r==1 && color(result,*placed[1].node).g==1,"Ancestor final appearance across partitions failed");
            Fixture::write(path,"parts/leaf.jt",big,true,false,true);result=Jt::Importer::read(path);
            placed.clear();walk(result.scene->mRootNode,aiMatrix4x4());
            check(placed.size()==2 && result.scene->mNumMeshes==2 && placed[0].world.a4==-30 && placed[1].world.a4==30,"Mixed wire/solid fixture placement failed");
            for(unsigned i=0;i<2;++i) {
                const auto mesh=result.scene->mMeshes[placed[i].node->mMeshes[0]];int wire=-1;
                check(result.scene->mMaterials[mesh->mMaterialIndex]->Get(AI_MATKEY_ENABLE_WIREFRAME,wire)==AI_SUCCESS && wire==int(i==0),"Mixed occurrences lost their material wireframe flags");
                check(mesh->mNumVertices==913 && mesh->mNumFaces==904,"Wireframe changed stored geometry counts");
            }
            check(color(result,*placed[0].node).r==1 && color(result,*placed[1].node).g==1,"Mixed wire/solid fixture colors failed");
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
