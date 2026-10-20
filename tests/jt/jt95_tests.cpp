// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original Mk.2 packets and assembly fixtures; no downloaded CAD geometry.
#include "JtLegacyFixture.h"
#include "Jt/JtImporter.h"
#include <JtDecode_Int32CDP.hxx>
#include <JtDecode_MeshCoderDriver.hxx>
#include <JtAttribute_Material.hxx>
#include <QTemporaryDir>
#include <QDir>
#include <assimp/material.h>
#include <iostream>

namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F>void rejects(F f){try{f();}catch(const Jt::Cancelled&){throw;}catch(const std::exception&){return;}throw std::runtime_error("Malformed JT 9.5 fixture accepted");}
struct Test95Model:JtData_Model {Test95Model(bool big,unsigned major=9):JtData_Model(TCollection_ExtendedString()){myMajorVersion=major;myMinorVersion=major==9?5:major==8?2:0;myIsFileLE=!big;}};
struct Reader:JtData_Reader {
 QByteArray data;size_t pos=0;
 Reader(const Fixture::Bytes& b,unsigned major=9):JtData_Reader(new Test95Model(b.big,major)),data(b.data){}
 Standard_Boolean ReadBytes(void* out,Standard_Size n)override{if(n>size_t(data.size())-pos)return false;if(n)std::memcpy(out,data.constData()+pos,n);pos+=n;return true;}
 Standard_Boolean SkipBytes(Standard_Size n)override{if(n>size_t(data.size())-pos)return false;pos+=n;return true;}
 Standard_Size GetPosition()const override{return pos;}
};
struct Bits {
 QByteArray data;unsigned used=0;
 void put(uint32_t v,unsigned width){for(unsigned i=width;i>0;--i){if(used%8==0)data.append(char(0));data[data.size()-1]=char(uint8_t(data.back())|(((v>>(i-1))&1)<<(7-used%8)));++used;}}
 void words(Fixture::Bytes& b){while(used%32)put(0,1);for(qsizetype i=0;i<data.size();i+=4){uint32_t word=0;for(int j=0;j<4;++j)word=(word<<8)|uint8_t(data[i+j]);b.u32(word);}}
};
Fixture::Bytes plain(bool big,std::initializer_list<int32_t> values){Fixture::Bytes b(big);b.u32(unsigned(values.size()));if(values.size()){b.u8(0);b.u32(unsigned(values.size()*32));for(auto v:values)b.u32(uint32_t(v));}return b;}
std::vector<int32_t> decode(const Fixture::Bytes& b){Reader r(b);JtDecode_Int32CDP p;check(p.Load2(r),"JT 9.5 packet load failed");Jt_VecI32 values(p.DecodeI32());check(r.pos==size_t(b.data.size()),"JT 9.5 packet left unread bytes");return std::vector<int32_t>(values.Data(),values.Data()+values.Count());}
Fixture::Bytes arithmetic(bool big,unsigned zeroEntry,bool allZero=false){
 Fixture::Bytes b(big);b.u32(4);b.u8(3);b.u32(20);b.u32(0x58000000);
 // Table frequencies 1,1 produce independently calculable 10,20,10,20.
 // Insert an unused escape at any position: its zero-width interval must
 // neither consume coded symbols nor change the decoded values.
 Bits context;context.put(3,16);context.put(2,6);context.put(1,6);context.put(4,6);context.put(10,32);
 unsigned value=0;
 for(unsigned i=0;i<3;++i){bool zero=i==zeroEntry;context.put(zero?0:2+value,2);context.put(zero||allZero?0:1,1);context.put(zero?0:10*value,4);if(!zero)++value;}
 b.data+=context.data;b.u32(0);return b; // Empty OOB packet (Mk.2 always carries it).
}
void codecTests(){
 for(bool big:{false,true}){
  check(decode(plain(big,{7,-1,INT32_MIN,INT32_MAX}))==std::vector<int32_t>({7,-1,INT32_MIN,INT32_MAX}),"Mk.2 null values changed");
  for(unsigned position=0;position<3;++position){auto b=arithmetic(big,position);check(decode(b)==std::vector<int32_t>({10,20,10,20}),"Mk.2 zero-frequency interval changed symbols");
   for(qsizetype n=0;n<b.data.size();++n){auto shortPacket=b;shortPacket.data.truncate(n);rejects([&]{decode(shortPacket);});}
  }
  rejects([&]{decode(arithmetic(big,0,true));});
  // Fixed-width codes: bias -2, max 1, followed by 0,1,2,3.
  Bits fixed;fixed.put(0,1);fixed.put(2,6);fixed.put(2,6);fixed.put(2,2);fixed.put(1,2);for(unsigned i=0;i<4;++i)fixed.put(i,2);
  Fixture::Bytes b(big);b.u32(4);b.u8(1);b.u32(fixed.used);fixed.words(b);check(decode(b)==std::vector<int32_t>({-2,-1,0,1}),"Mk.2 fixed-width bias mismatch");
  Bits wide;wide.put(0,1);wide.put(32,6);wide.put(32,6);wide.put(0x80000000,32);wide.put(0x7fffffff,32);wide.put(0,32);wide.put(0xffffffff,32);
  Fixture::Bytes full(big);full.u32(2);full.u8(1);full.u32(wide.used);wide.words(full);check(decode(full)==std::vector<int32_t>({INT32_MIN,INT32_MAX}),"Mk.2 full-width range overflow");
  for(unsigned damage=0;damage<4;++damage){Fixture::Bytes chopped(big);chopped.u32(3);chopped.u8(4);chopped.u8(damage==1?33:2);chopped.u32(uint32_t(-10));chopped.u8(damage==2?1:4);chopped.append(plain(big,{-1,1,2}));chopped.append(plain(big,{3,damage==3?4:0,2}));
   if(damage)rejects([&]{decode(chopped);});else check(decode(chopped)==std::vector<int32_t>({-11,-6,0}),"Mk.2 chopper signed reconstruction mismatch");}
  // Variable-width mean-only run, plus malformed widths, runs and field adjustments.
  for(unsigned damage=0;damage<5;++damage){Bits bits;bits.put(1,1);bits.put(7,32);bits.put(damage==1?0:2,3);bits.put(3,3);bits.put(damage==2?3:0,2);bits.put(damage==3?0:damage==4?5:4,3);
   Fixture::Bytes packet(big);packet.u32(4);packet.u8(1);packet.u32(bits.used);bits.words(packet);
   if(damage)rejects([&]{decode(packet);});else check(decode(packet)==std::vector<int32_t>({7,7,7,7}),"Mk.2 variable-width mean mismatch");}
 }
}
void topologyMaskTests(){
 for(bool big:{false,true})for(unsigned major:{9u,10u}){
  Fixture::Bytes b(big);
  for(unsigned stream=0;stream<24;++stream){
   if(major==10 && stream==20)continue;
   if(stream==21){b.u32(3);b.u32(1);b.u32(0);b.u32(0x80000000);}
   else if(stream==20)b.append(plain(big,{15}));
   else if(stream==22)b.append(plain(big,{7,-3}));
   else if(stream==23)b.append(plain(big,{11}));
   else b.u32(0);
  }
  b.u32(0x12345678);
  Reader reader(b,major);auto data=JtDecode_MeshCoderDriver::LoadInputData(reader);
  check(data.operator bool()&&reader.pos==size_t(b.data.size()),"Topology raw masks shifted following streams/hash");
  Jt_VecI32 masks(data->_vuOutAttrMasksLrg.DecodeI32());check(masks.Count()==3&&masks[0]==1&&masks[1]==0&&uint32_t(masks[2])==0x80000000,"High-degree masks were decoded as CDP");
  Jt_VecI32 split(data->_viOutSplitVtxSyms.DecodeI32()),position(data->_viOutSplitPosSyms.DecodeI32());check(split.Count()==2&&split[0]==7&&split[1]==-3&&position.Count()==1&&position[0]==11,"Raw masks corrupted split streams");
  if(major==9){Jt_VecI32 upper(data->_faceAttributeMask8_4.DecodeI32());check(upper.Count()==1&&upper[0]==15,"Legacy upper-four-bit stream lost");}
  for(qsizetype n=0;n<b.data.size();++n){auto shortPacket=b;shortPacket.data.truncate(n);rejects([&]{Reader r(shortPacket,major);check(JtDecode_MeshCoderDriver::LoadInputData(r).operator bool(),"Truncated topology input");});}
 }
}
void baseNode95(Fixture::Bytes& b,std::initializer_list<unsigned> attrs){b.u16(1);b.u32(0);b.list(attrs);}
Fixture::Bytes partition95(bool big,const QString& reference,std::initializer_list<unsigned> children,std::initializer_list<unsigned> attrs={}){Fixture::Bytes b(big);baseNode95(b,attrs);b.u16(1);b.list(children);b.u32(0);b.text(reference);for(int i=0;i<6;++i)b.f32(0);b.f32(0);for(int i=0;i<6;++i)b.u32(0);return b;}
QByteArray assembly(bool big,unsigned baseVersion=1,unsigned materialVersion=2,bool maskFixture=false){
 using namespace Fixture;Bytes graph(big);element(graph,type("10dd103e"),1,partition95(big,{}, maskFixture?std::initializer_list<unsigned>{10,11,12}:std::initializer_list<unsigned>{10,11},maskFixture?std::initializer_list<unsigned>{30}:std::initializer_list<unsigned>{}));element(graph,type("10dd103e"),2,partition95(big,"part.jt",{}));
 for(unsigned i=0;i<(maskFixture?3u:2u);++i){Bytes instance(big);baseNode95(instance,{20+i*2,21+i*2});instance.u16(1);instance.u32(2);element(graph,type("10dd102a"),10+i,instance);
  Bytes transform(big);transform.u16(1);transform.u8(8);transform.u32(0);transform.u16(1);transform.u16(8);transform.f64(100+100*i);element(graph,type("10dd1083"),20+i*2,transform,3);
  Bytes material(big);material.u16(baseVersion);material.u8(maskFixture?8:9);material.u32(maskFixture?(i==0?1u<<7:i==1?1u<<8:1u<<1):0);material.u16(materialVersion);material.u16(maskFixture?0x3990:0);
  for(unsigned c=0;c<4;++c)for(unsigned j=0;j<4;++j)material.f32(j==3?(maskFixture&&c==1?.25f:1.f):c==1?float(j==(maskFixture?1:i)):0.f);material.f32(15);if(materialVersion==2)material.f32(0);element(graph,type("10dd1030"),21+i*2,material,3);
 }
 if(maskFixture){Bytes parent(big);parent.u16(1);parent.u8(8);parent.u32(0);parent.u16(1);parent.u16(0x3990);for(unsigned c=0;c<4;++c)for(unsigned j=0;j<4;++j)parent.f32(j==3?1.f:c==1?float(j==0):0.f);parent.f32(15);element(graph,type("10dd1030"),30,parent,3);}
 end(graph);end(graph);graph.u16(1);graph.u32(0);
 const QString guid="12345678-1234-5678-9abc-def012345678";
 Bytes segment(big);segment.guid(guid);segment.u32(1);segment.u32(33+unsigned(graph.data.size()));segment.u32(0);segment.u32(1+unsigned(graph.data.size()));segment.u8(0);segment.append(graph);
 Bytes file(big);file.data=QByteArray("Version 9.5 JT original assembly fixture").leftJustified(80,' ');file.u8(big?1:0);file.u32(0);file.u32(105+unsigned(segment.data.size()));file.guid(guid);file.append(segment);
 file.u32(1);file.guid(guid);file.u32(105);file.u32(unsigned(segment.data.size()));file.u32(1u<<24);return file.data;
}
void write(const QString& path,const QByteArray& bytes){QFile f(path);check(f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size(),"Cannot write JT 9.5 fixture");}
void materialMaskTests(){
 const unsigned expected[]={1,192,2,4,8,16,32,64,128};
 for(bool big:{false,true})for(unsigned major:{8u,9u})for(unsigned bit=0;bit<(major==8?6u:9u);++bit){
  Fixture::Bytes b(big);if(major==9)b.u16(1);b.u8(8);b.u32(1u<<bit);if(major==9)b.u16(1);b.u16(0);
  for(unsigned c=0;c<4;++c)for(unsigned j=0;j<4;++j)b.f32(j==3?1.f:c==1?float(j==1):0.f);b.f32(15);
  Reader reader(b,major);JtAttribute_Material material;check(material.Read(reader)&&reader.pos==size_t(b.data.size()),"Legacy material mask fixture shifted payload");check(material.FieldInhibitFlags()==expected[bit],"Legacy material inhibit field mapped to wrong scene field");
 }
 QTemporaryDir dir;check(dir.isValid(),"Cannot create material-mask fixture directory");
 for(bool big:{false,true}){
  LegacyFixture::Options part;part.materialInhibit=0x3f;part.minorVersion=2;part.big=big;LegacyFixture::write(dir.filePath("part.jt"),part);
  const auto path=dir.filePath("masks.jt");write(path,assembly(big,1,2,true));auto result=Jt::Importer::read(path);unsigned count=0;
  const auto visit=[&](auto&& self,const aiNode* node,aiMatrix4x4 world)->void{world=world*node->mTransformation;for(unsigned i=0;i<node->mNumMeshes;++i){const auto mesh=result.scene->mMeshes[node->mMeshes[i]];const auto material=result.scene->mMaterials[mesh->mMaterialIndex];aiColor4D color;float alpha=-1;check(material->Get(AI_MATKEY_COLOR_DIFFUSE,color)==AI_SUCCESS&&material->Get(AI_MATKEY_OPACITY,alpha)==AI_SUCCESS,"Missing inhibited material");const int item=int((world.a4-110)/100);check(item>=0&&item<3,"Unexpected inhibit fixture placement");check(color.r==(item==1?0:1)&&color.g==(item==1?1:0)&&alpha==(item==0?.25f:1.f),"Combined/separate legacy diffuse-alpha inheritance failed");++count;}for(unsigned i=0;i<node->mNumChildren;++i)self(self,node->mChildren[i],world);};visit(visit,result.scene->mRootNode,aiMatrix4x4());check(count==3,"Material mask fixture lost occurrences");
 }
}
void assemblyTests(){
 QTemporaryDir dir;check(dir.isValid(),"Cannot create JT 9.5 fixture directory");const auto path=dir.filePath("assembly.jt");
 for(bool big:{false,true})for(unsigned version:{1,2}){
  LegacyFixture::Options part;part.big=big;part.minorVersion=2;LegacyFixture::write(dir.filePath("part.jt"),part);write(path,assembly(big,1,version));auto result=Jt::Importer::read(path);
  check(result.scene->mNumMeshes==2,"JT 9.5 occurrence colours lost resource separation");
  unsigned occurrences=0;
  const auto visit=[&](auto&& self,const aiNode* node,aiMatrix4x4 world)->void{world=world*node->mTransformation;for(unsigned i=0;i<node->mNumMeshes;++i){const auto mesh=result.scene->mMeshes[node->mMeshes[i]];check(mesh->mNumVertices==7&&mesh->mNumFaces==3,"Cross-version fixture geometry changed");aiColor4D color;check(result.scene->mMaterials[mesh->mMaterialIndex]->Get(AI_MATKEY_COLOR_DIFFUSE,color)==AI_SUCCESS,"Missing JT 9.5 colour");check((color.r==1&&color.g==0&&world.a4==110)||(color.g==1&&color.r==0&&world.a4==210),"JT 9.5 material/transform accumulation mismatch");check(world.b4==-3,"Cross-version local transform lost");++occurrences;}for(unsigned i=0;i<node->mNumChildren;++i)self(self,node->mChildren[i],world);};visit(visit,result.scene->mRootNode,aiMatrix4x4());check(occurrences==2,"JT 9.5 repeated-instance count mismatch");
  write(path,assembly(big,2,version));rejects([&]{Jt::Importer::read(path);});write(path,assembly(big,1,3));rejects([&]{Jt::Importer::read(path);});
  auto bad=assembly(big);bad.truncate(106);write(path,bad);rejects([&]{Jt::Importer::read(path);});
 }
}
}
void jt95Tests(){codecTests();topologyMaskTests();assemblyTests();materialMaskTests();std::cout<<"JT 9.5 LE/BE Mk.2 arithmetic/bitlength, malformed packets and cross-version assemblies passed\n";}
void jtMaterialMaskFixtureFiles(const QString& folder){check(QDir().mkpath(folder),"Cannot create material mask UI directory");LegacyFixture::Options part;part.minorVersion=2;part.materialInhibit=0x3f;LegacyFixture::write(QDir(folder).filePath("part.jt"),part);for(bool big:{false,true})write(QDir(folder).filePath(big?"material_masks_be.jt":"material_masks_le.jt"),assembly(big,1,2,true));}
void jt95FixtureFiles(const QString& folder){check(QDir().mkpath(folder),"Cannot create JT 9.5 fixture directory");LegacyFixture::Options part;part.minorVersion=2;LegacyFixture::write(QDir(folder).filePath("part.jt"),part);for(bool big:{false,true})write(QDir(folder).filePath(big?"assembly_be.jt":"assembly_le.jt"),assembly(big));}
