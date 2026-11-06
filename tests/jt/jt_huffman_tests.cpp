// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original hand-calculated Huffman packets; no vendor geometry or encoder code.
#include "JtContainerFixture.h"
#include "Jt/JtImporter.h"
#include <JtDecode_Int32CDP.hxx>
#include <JtData_Limits.hxx>
#include <iostream>
#include <vector>
namespace {
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
template<class F>void reject(F f){try{f();}catch(const Jt::Cancelled&){throw;}catch(const std::exception&){return;}throw std::runtime_error("Invalid Huffman packet accepted");}
struct HuffmanTestModel:JtData_Model {HuffmanTestModel(bool big,unsigned minor):JtData_Model(TCollection_ExtendedString()){myMajorVersion=8;myMinorVersion=minor;myIsFileLE=!big;}};
struct Reader:JtData_Reader {
 QByteArray data;size_t pos=0;Reader(const Fixture::Bytes& b,unsigned minor=2):JtData_Reader(new HuffmanTestModel(b.big,minor)),data(b.data){}
 Standard_Boolean ReadBytes(void* p,Standard_Size n)override{if(n>size_t(data.size())-pos)return false;if(n)std::memcpy(p,data.constData()+pos,n);pos+=n;return true;}
 Standard_Boolean SkipBytes(Standard_Size n)override{if(n>size_t(data.size())-pos)return false;pos+=n;return true;}
 Standard_Size GetPosition()const override{return pos;}
};
struct Bits {QByteArray data;unsigned used=0;void put(uint32_t v,unsigned n){for(unsigned i=n;i>0;--i){if(used%8==0)data.append(char(0));data[data.size()-1]=char(uint8_t(data.back())|(((v>>(i-1))&1)<<(7-used%8)));++used;}}};
struct Entry {int symbol;uint32_t frequency,value;};
Fixture::Bytes packet(bool big,std::initializer_list<Entry> entries,unsigned count,unsigned bitCount,uint32_t code,std::initializer_list<int> oob={}){
 Fixture::Bytes b(big);b.u8(2);b.u8(1);Bits bits;
 bits.put(unsigned(entries.size()),32);bits.put(3,6);bits.put(32,6);bits.put(5,6);bits.put(0,6);bits.put(0,32);
 for(auto e:entries){bits.put(uint32_t(e.symbol+2),3);bits.put(e.frequency,32);bits.put(e.value,5);}
 b.data+=bits.data;b.u32(unsigned(oob.size()));
 if(oob.size()){b.u8(0);b.u32(unsigned(oob.size()));for(int x:oob)b.u32(uint32_t(x));}
 b.u32(bitCount);b.u32(count);b.u32((bitCount+31)/32);for(unsigned word=0;word<(bitCount+31)/32;++word)b.u32(code);return b;
}
std::vector<int> decode(const Fixture::Bytes& b,unsigned minor=2){Reader r(b,minor);JtDecode_Int32CDP p;check(p.Load1(r),"Huffman load failed");Jt_VecI32 v(p.DecodeI32());check(r.pos==size_t(b.data.size()),"Huffman envelope mismatch");return std::vector<int>(v.Data(),v.Data()+v.Count());}
}
void huffmanTests(){
 for(unsigned minor:{0u,1u,2u})for(bool big:{false,true}){
  // Frequencies 1/2/4 yield codes 11/10/0 (left=1). 11100 -> 10,20,30.
  auto b=packet(big,{{0,1,10},{1,2,20},{2,4,30}},3,5,0xe0000000);
  check(decode(b,minor)==std::vector<int>({10,20,30}),"Huffman frequency/tree/endian mismatch");
  check(decode(packet(big,{{0,1,10},{1,1,20}},4,4,0xa0000000),minor)==std::vector<int>({10,20,10,20}),"Huffman equal-frequency ordering changed");
  check(decode(packet(big,{{0,1,10},{1,1,20},{2,1,30}},6,10,0x4c400000),minor)==std::vector<int>({10,20,30,30,20,10}),"Huffman three-way tie ordering changed");
  const auto multiword=decode(packet(big,{{0,1,10},{1,1,20}},40,40,0xaaaaaaaa),minor);
  for(unsigned i=0;i<40;++i)check(multiword[i]==(i%2?20:10),"Huffman cross-word decoding failed");
  check(decode(packet(big,{{0,1,7}},4,0,0),minor)==std::vector<int>({7,7,7,7}),"Huffman single-symbol tree failed");
  check(decode(packet(big,{{0,1,7},{-2,2,0}},4,4,0xa0000000,{41,42}),minor)==std::vector<int>({7,41,7,42}),"Huffman escape values lost");
  check(decode(packet(big,{{-2,1,0}},2,0,0,{41,42}),minor)==std::vector<int>({41,42}),"Huffman single escape failed");
  check(decode(packet(big,{{0,0x80000000u,10},{1,1,20}},2,2,0x80000000),minor)==std::vector<int>({20,10}),"Huffman unsigned frequency ordering overflowed");
  for(qsizetype n=0;n<b.data.size();++n){auto truncated=b;truncated.data.truncate(n);reject([&]{decode(truncated,minor);});}
  reject([&]{decode(packet(big,{{0,1,10},{1,2,20},{2,4,30}},3,4,0xe0000000),minor);}); // padding cannot supply the fifth bit
  reject([&]{decode(packet(big,{{0,1,10},{1,2,20}},1,0,0),minor);});
  reject([&]{decode(packet(big,{{-2,1,0}},2,0,0,{41}),minor);});
  reject([&]{decode(packet(big,{{-2,1,0}},1,0,0,{41,42}),minor);});
  reject([&]{decode(packet(big,{{0,0,7}},1,0,0),minor);});
  reject([&]{decode(packet(big,{},1,0,0),minor);});
  auto multiple=b;multiple.data[1]=2;reject([&]{decode(multiple,minor);});
  Reader r(b,minor);JtDecode_Int32CDP p;check(p.Load1(r),"Cannot load cancellation fixture");
  {bool stopped=false;JtData_Limits::Scope scope([]{throw Jt::Cancelled();});try{Jt_VecI32 v(p.DecodeI32());}catch(const Jt::Cancelled&){stopped=true;}check(stopped,"Huffman ignored cancellation");}
  {JtData_Limits::Scope scope({},32);reject([&]{Jt_VecI32 v(p.DecodeI32());});}
 }
 std::cout<<"Huffman 8.0-8.2 LE/BE trees, ties, singleton, escapes, declared-bit limits, truncation, cancellation and budgets passed\n";
}
