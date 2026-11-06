// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original hand-calculated context-transition packets and geometry controls.
#include "JtContainerFixture.h"
#include "JtLegacyFixture.h"
#include "Jt/JtImporter.h"
#include <JtDecode_Int32CDP.hxx>
#include <JtData_Limits.hxx>
#include <QTemporaryDir>
#include <iostream>
#include <vector>
namespace {
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class F>void reject(F action){try{action();}catch(const Jt::Cancelled&){throw;}catch(const std::exception&){return;}throw std::runtime_error("Invalid arithmetic count packet accepted");}
struct ArithmeticCountModel:JtData_Model {ArithmeticCountModel(bool big,unsigned minor):JtData_Model(TCollection_ExtendedString()){myMajorVersion=8;myMinorVersion=minor;myIsFileLE=!big;}};
struct Reader:JtData_Reader {
 QByteArray bytes;size_t pos=0;
 Reader(const Fixture::Bytes& b,unsigned minor):JtData_Reader(new ArithmeticCountModel(b.big,minor)),bytes(b.data){}
 Standard_Boolean ReadBytes(void* p,Standard_Size n)override{if(n>size_t(bytes.size())-pos)return false;if(n)std::memcpy(p,bytes.constData()+pos,n);pos+=n;return true;}
 Standard_Boolean SkipBytes(Standard_Size n)override{if(n>size_t(bytes.size())-pos)return false;pos+=n;return true;}
 Standard_Size GetPosition()const override{return pos;}
};
struct Bits {QByteArray bytes;unsigned used=0;void put(uint32_t v,unsigned n){for(unsigned i=n;i>0;--i){if(used%8==0)bytes.append(char(0));bytes[bytes.size()-1]=char(uint8_t(bytes.back())|(((v>>(i-1))&1)<<(7-used%8)));++used;}}};
Fixture::Bytes packet(bool big,int values,int symbols,const std::vector<int>& oob,bool ordinary=false,bool loop=false,unsigned next=1){
 Fixture::Bytes b(big);b.u8(3);b.u8(2);Bits bits;
 // First alphabet: escape and ordinary 10, each frequency 1. A zero code
 // chooses escape; all-one code chooses ordinary. Both enter context 1.
 bits.put(2,32);bits.put(3,6);bits.put(1,6);bits.put(4,6);bits.put(2,6);bits.put(0,32);
 for(unsigned symbol:{0u,2u}){bits.put(symbol,3);bits.put(1,1);bits.put(symbol?10:0,4);bits.put(next,2);}
 // Singleton second context inherits the first alphabet's associated value.
 bits.put(1,32);bits.put(3,6);bits.put(1,6);bits.put(2,6);
 bits.put(ordinary?2:0,3);bits.put(1,1);bits.put(loop?1:0,2);
 b.data+=bits.bytes;b.u32(unsigned(oob.size()));
 if(!oob.empty()){b.u8(0);b.u32(unsigned(oob.size()));for(int v:oob)b.u32(uint32_t(v));}
 b.u32(32);b.u32(uint32_t(values));b.u32(uint32_t(symbols));b.u32(1);b.u32(ordinary?0xffffffffu:0);return b;
}
std::vector<int> decode(const Fixture::Bytes& b,unsigned minor){Reader r(b,minor);JtDecode_Int32CDP p;check(p.Load1(r),"Cannot load arithmetic count packet");Jt_VecI32 values(p.DecodeI32());check(r.pos==size_t(b.data.size()),"Arithmetic count envelope mismatch");return {values.Data(),values.Data()+values.Count()};}
}
void arithmeticCountTests(){
 for(unsigned minor:{0u,1u,2u})for(bool big:{false,true}){
  const auto b=packet(big,3,6,{4,7,11});
  check(decode(b,minor)==std::vector<int>({4,7,11}),"Arithmetic context-0 escapes or trailing context-1 escape changed");
  check(decode(packet(big,3,5,{4,7,11}),minor)==std::vector<int>({4,7,11}),"Arithmetic declared final emitting symbol changed");
  check(decode(packet(big,3,3,{},true),minor)==std::vector<int>({10,10,10}),"Arithmetic ordinary symbols/inherited associated values changed");
  check(decode(packet(big,1,5000,{23},false,true),minor)==std::vector<int>({23}),"Declared non-emitting tail stopped at old heuristic");
  for(int symbols:{-1,0,2,4,7,8,16000001})reject([&]{decode(packet(big,3,symbols,{4,7,11}),minor);});
  reject([&]{decode(packet(big,3,6,{4,7}),minor);});
  reject([&]{decode(packet(big,3,6,{4,7,11,19}),minor);});
  reject([&]{decode(packet(big,3,6,{4,7,11},false,false,2),minor);});
  for(qsizetype size=0;size<b.data.size();++size){auto shortData=b;shortData.data.truncate(size);reject([&]{decode(shortData,minor);});}
  auto tail=packet(big,1,5000,{23},false,true);Reader reader(tail,minor);JtDecode_Int32CDP p;check(p.Load1(reader),"Cannot load arithmetic cancellation packet");
  {bool stopped=false;unsigned polls=0;JtData_Limits::Scope scope([&]{if(++polls==3)throw Jt::Cancelled();});try{Jt_VecI32 v(p.DecodeI32());}catch(const Jt::Cancelled&){stopped=true;}check(stopped,"Arithmetic non-emitting tail ignored cancellation");}
  {Reader budgetReader(b,minor);JtDecode_Int32CDP budgetPacket;check(budgetPacket.Load1(budgetReader),"Cannot load arithmetic budget packet");JtData_Limits::Scope scope({},0);reject([&]{Jt_VecI32 v(budgetPacket.DecodeI32());});}
 }
 QTemporaryDir dir;
 for(unsigned minor:{0u,1u,2u})for(bool big:{false,true}){
  const auto path=dir.filePath("counts.jt");LegacyFixture::Options o;o.minorVersion=minor;o.big=big;o.codec=3;LegacyFixture::write(path,o);
  const auto result=Jt::Importer::read(path);const auto& mesh=*result.scene->mMeshes[0];
  check(result.scene->mNumMeshes==1&&mesh.mNumVertices==7&&mesh.mNumFaces==3,"Arithmetic count model geometry changed");
  auto plain=o;plain.codec=0;const auto referencePath=dir.filePath("reference.jt");LegacyFixture::write(referencePath,plain);
  const auto reference=Jt::Importer::read(referencePath);const auto& expected=*reference.scene->mMeshes[0];
  for(unsigned i=0;i<mesh.mNumVertices;++i)check(mesh.mVertices[i]==expected.mVertices[i]&&mesh.mNormals[i]==expected.mNormals[i],"Arithmetic controls changed coordinates/normals");
  for(unsigned i=0;i<mesh.mNumFaces;++i)for(unsigned j=0;j<3;++j)check(mesh.mFaces[i].mIndices[j]==expected.mFaces[i].mIndices[j],"Arithmetic controls changed strip topology");
  for(int count:{4,7}){o.arithmeticSymbolCount=count;LegacyFixture::write(path,o);reject([&]{Jt::Importer::read(path);});}
 }
 std::cout<<"CDP1 arithmetic symbol/value counts, non-emitting tails, OOB bounds, cancellation, truncation and 8.x full models passed\n";
}
void arithmeticCountFixtureFiles(const QString& folder){
 check(QDir().mkpath(folder),"Cannot create arithmetic fixture directory");QDir dir(folder);
 for(unsigned minor:{0u,1u,2u})for(bool big:{false,true}){
  const auto stem=QString("arithmetic_8_%1_%2").arg(minor).arg(big?"be":"le");LegacyFixture::Options o;o.minorVersion=minor;o.big=big;o.codec=3;
  LegacyFixture::write(dir.filePath(stem+".jt"),o);o.codec=0;LegacyFixture::write(dir.filePath(stem+"_reference.jt"),o);
 }
 LegacyFixture::Options bad;bad.codec=3;bad.minorVersion=2;bad.arithmeticSymbolCount=4;LegacyFixture::write(dir.filePath("invalid_short_count.jt"),bad);
 bad.arithmeticSymbolCount=7;LegacyFixture::write(dir.filePath("invalid_long_count.jt"),bad);
 Fixture::writeSelection(dir.filePath("count_lod_fallback.jt"),"invalid_short_count.jt","arithmetic_8_2_le.jt",true);
 const auto fallback=Jt::Importer::read(dir.filePath("count_lod_fallback.jt"));check(fallback.scene->mNumMeshes==1&&!fallback.warnings.isEmpty(),"Arithmetic count error lost usable LOD fallback");
}
