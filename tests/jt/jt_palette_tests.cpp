// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original palette-state and per-face rendering fixtures, no downloaded geometry.
#include "JtSceneBuilder.h"
#include "JtContainerFixture.h"
#include <JtAttribute_PaletteMap.hxx>
#include <JtData_Reader.hxx>
#include <JtDecode_Int32CDP.hxx>
#include <JtAttribute_Material.hxx>
#include <JtAttribute_GeometricTransform.hxx>
#include <JtNode_Partition.hxx>
#include <JtNode_Instance.hxx>
#include <JtNode_Shape_Base.hxx>
#include <JtElement_ShapeLOD_Vertex.hxx>
#include <JtProperty_LateLoaded.hxx>
#include <JtProperty_String.hxx>
#include <QTemporaryDir>
#include <QDir>
#include <assimp/material.h>
#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
#include <iostream>
namespace {
using Object=Handle(JtData_Object);
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
template<class F>void rejects(F f){try{f();}catch(const Jt::Cancelled&){throw;}catch(const std::exception&){return;}throw std::runtime_error("Invalid palette case accepted");}
template<class T>struct Node:T {
 void attrs(std::initializer_list<Object> list){this->myAttributes.Allocate(list.size());int n=0;for(auto& a:list)this->myAttributes[n++]=a;}
 void children(std::initializer_list<Object> list){this->myChildren.Allocate(list.size());int n=0;for(auto& a:list)this->myChildren[n++]=a;}
 void late(Object p){this->myLateLoads.Allocate(1);this->myLateLoads[0]=Handle(JtProperty_LateLoaded)::DownCast(p);}
 void target(Object p){this->myObject=p;}
};
struct Material:JtAttribute_Material {
 void ordinary(){JtAttribute_Base::myVersion=2;myDataFlags=0x3980;}
 Material(int pi,aiColor4D c,unsigned inhibit=0,unsigned finals=0,unsigned state=8){
  myPaletteIndex=pi;myVersion=1;myStateFlags=state;myFieldInhibitFlags=inhibit;myFieldFinalFlags=finals;myDataFlags=0x39a0;
  for(unsigned i=0;i<4;++i){myDiffuseColor[i]=(&c.r)[i];myAmbientColor[i]=0;mySpecularColor[i]=0;myEmissionColor[i]=0;}
  myShininess=15;myReflectivity=0;myBumpiness=1;
 }
};
struct Transform:JtAttribute_GeometricTransform {
 void noise(double value){myTransform[3]=value;myTransform[15]=1+value;}
 Transform(int pi,double x,unsigned finals=0,unsigned state=8){myPaletteIndex=pi;myStateFlags=state;myFieldFinalFlags=finals;for(int i=0;i<16;++i)myTransform[i]=i%5==0?1:0;myTransform[12]=x;}
};
struct Mapping:JtAttribute_PaletteMap {
 Mapping(std::initializer_list<int> list,unsigned inhibit=0,unsigned finals=0,unsigned state=8){myStateFlags=state;myFieldInhibitFlags=inhibit;myFieldFinalFlags=finals;myEntries.Allocate(list.size());int n=0;for(int v:list)myEntries[n++]=v;}
};
struct Triangles:JtElement_ShapeLOD_Vertex {
 Triangles(){myVertices.Allocate(9,3);myNormals.Allocate(9,3);myIndices.Allocate(9);myFaceGroups.Allocate(3);
  for(int i=0;i<9;++i){myIndices[i]=i;myVertices[i][0]=float((i/3)*3+(i%3==1));myVertices[i][1]=float(i%3==2);myVertices[i][2]=0;myNormals[i][0]=myNormals[i][1]=0;myNormals[i][2]=1;}for(int i=0;i<3;++i)myFaceGroups[i]=i;
  for(unsigned c=0;c<4;++c){myTexCoords[c].Allocate(9,2);for(unsigned i=0;i<9;++i){myTexCoords[c][i][0]=myVertices[i][0]*float(c+1);myTexCoords[c][i][1]=myVertices[i][1]+float(c);}}
 }
};
struct Property:JtProperty_LateLoaded {Property(){mySegmentType=7;myDefferedObject=new Triangles;}};
auto root(){return opencascade::handle<Node<JtNode_Partition>>(new Node<JtNode_Partition>);}
auto leaf(){opencascade::handle<Node<JtNode_Shape_Base>> n=new Node<JtNode_Shape_Base>;n->late(new Property);return n;}
struct Occurrence {unsigned mesh;aiMatrix4x4 world;const aiNode* node;};
std::vector<Occurrence> occurrences(const aiScene& scene){std::vector<Occurrence> out;std::function<void(const aiNode*,aiMatrix4x4)> walk=[&](const aiNode* n,aiMatrix4x4 w){w*=n->mTransformation;for(unsigned i=0;i<n->mNumMeshes;++i)out.push_back({n->mMeshes[i],w,n});for(unsigned i=0;i<n->mNumChildren;++i)walk(n->mChildren[i],w);};walk(scene.mRootNode,aiMatrix4x4());return out;}
aiColor4D colour(const aiScene& s,unsigned mesh){aiColor4D c;check(s.mMaterials[s.mMeshes[mesh]->mMaterialIndex]->Get(AI_MATKEY_COLOR_DIFFUSE,c)==AI_SUCCESS,"Missing palette colour");return c;}
}
void checkPaletteUV(const aiScene& scene){
 for(unsigned m=0;m<scene.mNumMeshes;++m){const auto& mesh=*scene.mMeshes[m];for(unsigned c=0;c<4;++c){check(mesh.HasTextureCoords(c),"Palette subset lost UV channel");for(unsigned i=0;i<mesh.mNumVertices;++i)check(mesh.mTextureCoords[c][i]==aiVector3D(mesh.mVertices[i].x*float(c+1),mesh.mVertices[i].y+float(c),0),"Palette subset remapped UV incorrectly");}}
}
void duplicateMaterialCompatibilityTests(){
 QTemporaryDir tmp;const auto path=tmp.filePath("source.jt");
 struct Text:JtProperty_String {Text(const char* value){myValue=TCollection_ExtendedString(value);}};
 const auto author=[](JtNode_Base& node,const char* value){node.BindProperty(1,2,new Text("CAD_SOURCE::"),new Text(value));};
 const aiColor4D red{1,0,0,1},green{0,1,0,1},blue{0,0,1,1};
 for(const char* signature:{"Version 10.5 JT ISO14306-4 DM 10.8.0.1",
                             "Version 10.7 JT DM 10.8.0.1",
                             "Version 10.8 JT DM 10.8.0.1",
                             "Version 10.9 JT DM 10.8.0.1",
                             "Version 10.10 JT DM 10.8.0.1"}) {
 for(unsigned kind=0;kind<15;++kind){
  QFile f(path);check(f.open(QIODevice::WriteOnly),"Cannot write source-profile test header");
  auto marker=QByteArray(signature);
  if(kind==2)marker.replace("10.8.0.1","10.7.0.1");
  if(kind==3)marker="Version 10.3 JT DM 10.8.0.1";
  const auto header=marker.leftJustified(75,' ')+QByteArray(" \n\r\n ",5);
  check(f.write(header)==80,"Cannot write source-profile marker");f.close();
  auto r=root();auto shape=leaf();r->children({shape});if(kind!=1)author(*r,kind==4?"Other CAD":"Solid Edge");
  auto first=new Material(-1,red);auto second=new Material(kind==8?0:-1,green,kind==6?1u:0,kind==12?1u<<6:0,kind==7?10:kind==13?9:8);first->ordinary();second->ordinary();
  shape->attrs({first,second});
  if(kind==14){r->attrs({first,second});shape->attrs({});}
  if(kind==9)shape->attrs({first,second,new Material(-1,blue)});
  if(kind==10)r->attrs({new Mapping({-1,-1,-1})});
  if(kind==11)r->attrs({new Material(0,blue)});
  Jt::Options options;options.duplicateFallbackMaterialCompatibility=kind!=5;
  const auto result=Jt::Detail::buildScene(r,path,options);const auto occ=occurrences(*result.scene);
  check(!occ.empty(),"Source compatibility discarded geometry");
  const auto expected=kind==0||kind==8?red:kind==9?blue:green;
  check(colour(*result.scene,occ[0].mesh)==expected,"Source material compatibility escaped its qualified scope");
  bool diagnostic=false;for(const auto& warning:result.warnings)diagnostic=diagnostic||warning.contains("compatibility profile");
  check(diagnostic==(kind==0),"Source compatibility diagnostic missing or spurious");
 }
 // Author metadata affects descendants of that branch only.
 {auto r=root();auto a=leaf(),b=leaf();author(*a,"Solid Edge");
  for(auto shape:{a,b}){auto x=new Material(-1,red);auto y=new Material(-1,green);x->ordinary();y->ordinary();shape->attrs({x,y});}
  r->children({a,b});const auto result=Jt::Detail::buildScene(r,path,{});checkPaletteUV(*result.scene);const auto occ=occurrences(*result.scene);
  check(occ.size()==2&&colour(*result.scene,occ[0].mesh)==red&&colour(*result.scene,occ[1].mesh)==green,"Source author profile leaked across sibling branches");}
 }
 std::cout<<"Qualified-writer 10.5/10.7/10.8/10.9/10.10 duplicate-material profile, opt-out, writer/author/flag/palette guards and sibling isolation passed\n";
}
void paletteTests(){
 QTemporaryDir tmp;const auto path=tmp.filePath("assembly.jt");
 const aiColor4D red{1,0,0,1},green{0,1,0,1},blue{0,0,1,1};
 for(int kind=0;kind<8;++kind){auto r=root();auto shape=leaf();r->children({shape});
  if(kind==0)r->attrs({new Material(-1,blue),new Material(0,red),new Material(1,green),new Mapping({0,1,-1})});
  if(kind==1)r->attrs({new Material(0,red),new Material(-1,green),new Mapping({0,0,0})});
  if(kind==2){r->attrs({new Material(-1,blue),new Material(0,red,0,1u<<6),new Mapping({0,0,0})});shape->attrs({new Material(0,green)});}
  if(kind==3){r->attrs({new Material(-1,blue),new Material(0,red,0,1u<<6),new Mapping({0,0,0})});shape->attrs({new Material(0,green,0,0,10)});}
  if(kind==4)r->attrs({new Material(-1,blue),new Material(0,red,1u<<6),new Mapping({0,0,0})});
  if(kind==5)r->attrs({new Material(-1,blue),new Material(0,red),new Mapping({0})});
  if(kind==6)r->attrs({new Material(-1,blue),new Material(0,red),new Mapping({0,-2,-1})});
  if(kind==7)r->attrs({new Material(-1,blue),new Mapping({17,17,17})});
  auto result=Jt::Detail::buildScene(r,path,{});checkPaletteUV(*result.scene);const auto occ=occurrences(*result.scene);check(occ.size()==(kind==0||kind==6?3:1),"Palette material-group occurrence count mismatch");
  for(unsigned i=0;i<occ.size();++i){const auto c=colour(*result.scene,occ[i].mesh);const auto expected=kind==0?(i==0?red:i==1?green:blue):kind==1||kind==3?green:kind==2?red:kind==6&&i==0?red:blue;check(c==expected,"Palette accumulation/fallback/final/inhibit colour mismatch");}
  check(result.hiddenNodes.size()==(kind==6?1:0),"Inhibited face-group visibility lost");
  unsigned triangles=0;for(unsigned i=0;i<result.scene->mNumMeshes;++i)triangles+=result.scene->mMeshes[i]->mNumFaces;check(triangles==3,"Palette split lost triangles or inhibited geometry");
  if(kind==0){Assimp::Exporter e;const auto glb=tmp.filePath("palette.glb");check(e.Export(result.scene.get(),"glb2",glb.toStdString())==AI_SUCCESS,"Palette GLB export failed");Assimp::Importer imp;const auto* read=imp.ReadFile(glb.toStdString(),0);check(read&&occurrences(*read).size()==3,"Palette GLB lost groups");checkPaletteUV(*read);}
 }
 {auto r=root();auto shape=leaf();r->children({shape});r->attrs({new Transform(-1,10),new Material(0,red),new Transform(0,30),new Mapping({0,-1,0})});shape->attrs({new Transform(-1,5)});auto result=Jt::Detail::buildScene(r,path,{});checkPaletteUV(*result.scene);const auto occ=occurrences(*result.scene);check(occ.size()==2&&occ[0].world.a4==45&&occ[1].world.a4==15,"Palette/fallback transform composition mismatch");}
 {auto r=root();auto shape=leaf();opencascade::handle<Node<JtNode_Instance>> a=new Node<JtNode_Instance>,b=new Node<JtNode_Instance>;a->target(shape);b->target(shape);a->attrs({new Transform(-1,10)});b->attrs({new Transform(-1,20)});r->children({a,b});r->attrs({new Material(0,red),new Mapping({0,0,0})});auto result=Jt::Detail::buildScene(r,path,{});checkPaletteUV(*result.scene);const auto occ=occurrences(*result.scene);check(result.scene->mNumMeshes==1&&occ.size()==2,"Palette repeated instances lost resource sharing");check(occ[0].world.a4==10&&occ[1].world.a4==20,"Palette states leaked between occurrences");}
 {auto r=root();auto shape=leaf();r->children({shape});r->attrs({new Transform(0,0),new Mapping({0,0,0})});Jt::Options cancel;cancel.cancelled=[] {return true;};bool stopped=false;try{Jt::Detail::buildScene(r,path,cancel);}catch(const Jt::Cancelled&){stopped=true;}check(stopped,"Palette traversal swallowed cancellation");}
 for(double noise:{1e-18,1e-6}) {auto r=root();auto shape=leaf();r->children({shape});auto t=new Transform(-1,10);t->noise(noise);r->attrs({t});if(noise<1e-12)Jt::Detail::buildScene(r,path,{});else rejects([&]{Jt::Detail::buildScene(r,path,{});});}
 // A single escape symbol produces the sole explicit OOB value. External
 // compression stores this OOB array raw; uncompressed packets nest CDP data.
 struct OobModel:JtData_Model {OobModel(bool big):JtData_Model(TCollection_ExtendedString()){myMajorVersion=10;myMinorVersion=0;myIsFileLE=!big;}};
 struct Reader:JtData_Reader {QByteArray bytes;size_t pos=0;Reader(const Fixture::Bytes& b,bool compressed):JtData_Reader(new OobModel(b.big)),bytes(b.data){SetExternallyCompressed(compressed);}Standard_Boolean ReadBytes(void* out,Standard_Size n)override {if(n>size_t(bytes.size())-pos)return false;std::memcpy(out,bytes.constData()+pos,n);pos+=n;return true;}Standard_Boolean SkipBytes(Standard_Size n)override{if(n>size_t(bytes.size())-pos)return false;pos+=n;return true;}Standard_Size GetPosition()const override{return pos;}};
 for(bool big:{false,true})for(bool compressed:{false,true}){
  Fixture::Bytes b(big);b.u32(1);b.u8(3);b.u32(32);b.u32(0);
  QByteArray context(8, char(0));unsigned bit=0;
  const auto put=[&](unsigned value,unsigned width){for(int i=int(width)-1;i>=0;--i){if((value>>i)&1)context[bit/8]=char(uint8_t(context[bit/8])|(0x80>>(bit%8)));++bit;}};
  put(1,16);put(1,6);put(1,7);put(0,32);put(1,1);put(1,1);put(0,1);b.data+=context;
  b.u32(1);if(!compressed){b.u8(0);b.u32(32);}b.u32(1234);
  Reader reader(b,compressed);JtDecode_Int32CDP packet;check(packet.Load2(reader),"Cannot load arithmetic escape fixture");Jt_VecI32 decoded(packet.DecodeI32());check(decoded.Count()==1&&decoded[0]==1234&&reader.pos==size_t(b.data.size()),"Raw/nested arithmetic OOB layout mismatch");
  for(qsizetype n=13;n<b.data.size();++n){auto truncated=b;truncated.data.truncate(n);rejects([&]{Reader shortReader(truncated,compressed);JtDecode_Int32CDP shortPacket;check(shortPacket.Load2(shortReader),"Expected truncated packet");Jt_VecI32 values(shortPacket.DecodeI32());});}
 }
 // Full binary palette records: byte order, explicit/omitted fallback tail and malformed fields.
 for(unsigned minor:{3u,5u,7u,8u,10u})for(bool big:{false,true})for(bool tail:{false,true}){
  const auto body=[&](int bad=0){Fixture::Bytes b(big);b.u8(2);b.u8(8);b.u32(0);b.u32(0);b.u8(bad==1?2:1);b.u32(3);b.u8(0);b.u32(96);b.u32(0);b.u32(quint32(-1));b.u32(quint32(bad==2?-3:-2));if(tail)b.u32(quint32(bad==3?0:-1));return b;};
  const auto read=[&](const Fixture::Bytes& payload){Fixture::Bytes graph(big);Fixture::element(graph,Fixture::type("10dd103e"),1,Fixture::partition(big,{}, {}, {20}));Fixture::element(graph,Fixture::type("10dd1106"),20,payload,3);Fixture::end(graph);Fixture::end(graph);graph.u16(1);graph.u32(0);Fixture::container(path,graph,minor);const auto utf=path.toStdU16String();Handle(JtData_Model) m=new JtData_Model(TCollection_ExtendedString(reinterpret_cast<const Standard_ExtCharacter*>(utf.c_str())));auto node=m->Init();check(!node.IsNull(),"Palette record failed to parse");auto map=Handle(JtAttribute_PaletteMap)::DownCast(node->Attributes()[0]);check(!map.IsNull()&&map->Entries().Count()==3&&map->Entries()[2]==-2,"Palette packet values shifted");};
  read(body());rejects([&]{read(body(1));});rejects([&]{read(body(2));});if(tail)rejects([&]{read(body(3));});
  auto shortTail=body();if(tail){shortTail.data.chop(1);rejects([&]{read(shortTail);});}
 }
 std::cout<<"Palette packet/tail bounds, inheritance, fallback/inhibition, transforms, shared resources and GLB passed\n";
}

void paletteFixtureFiles(const QString& folder) {
 check(QDir().mkpath(folder),"Cannot create palette fixture folder");
 auto r=root();auto shape=leaf();r->children({shape});
 r->attrs({new Material(-1,{0,0,1,1}),new Material(0,{1,0,0,1}),new Material(1,{0,1,0,1}),new Mapping({0,1,-1})});
 auto result=Jt::Detail::buildScene(r,QDir(folder).filePath("synthetic.jt"),{});
 Assimp::Exporter exporter;const auto output=QDir(folder).filePath("palette_three_colours.glb");
 check(exporter.Export(result.scene.get(),"glb2",output.toStdString())==AI_SUCCESS,"Cannot export original palette fixture");
}
