// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original known-coordinate fields; no downloaded geometry or encoder code.
#include "JtContainerFixture.h"
#include "Jt/JtImporter.h"
#include <JtDecode_VertexData.hxx>
#include <JtData_Limits.hxx>
#include <cmath>
#include <iostream>
#include <vector>
namespace {
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
template<class F>void reject(F f){try{f();}catch(const Jt::Cancelled&){throw;}catch(const std::exception&){return;}throw std::runtime_error("Invalid compressed-coordinate packet accepted");}
struct CoordinateModel:JtData_Model {CoordinateModel(bool big,unsigned major,unsigned minor):JtData_Model(TCollection_ExtendedString()){myMajorVersion=major;myMinorVersion=minor;myIsFileLE=!big;}};
struct Reader:JtData_Reader {
 QByteArray bytes;size_t pos=0;
 Reader(const Fixture::Bytes& b,unsigned major,unsigned minor):JtData_Reader(new CoordinateModel(b.big,major,minor)),bytes(b.data){}
 Standard_Boolean ReadBytes(void* p,Standard_Size n)override{if(n>size_t(bytes.size())-pos)return false;if(n)std::memcpy(p,bytes.constData()+pos,n);pos+=n;return true;}
 Standard_Boolean SkipBytes(Standard_Size n)override{if(n>size_t(bytes.size())-pos)return false;pos+=n;return true;}
 Standard_Size GetPosition()const override{return pos;}
};
void packet(Fixture::Bytes& b,const std::vector<uint32_t>& values){b.u32(unsigned(values.size()));b.u8(0);b.u32(unsigned(values.size()*32));for(size_t i=0;i<values.size();++i)b.u32(i>=4?values[i]-values[i-1]:values[i]);}
Fixture::Bytes stream(bool big,unsigned width,unsigned damage=0){
 Fixture::Bytes b(big);b.u32(damage==1?0:damage==2?16000001:6);b.u8(damage==3?4:3);
 for(unsigned c=0;c<3;++c){b.f32(c==2?3:-2);b.f32(c==2?(damage==4?4:3):5);b.u8(damage==4&&c==2?0:damage==5?33:damage==6&&c==0?0:width);}
 const auto maximum=uint32_t((uint64_t(1)<<width)-1);
 for(unsigned c=0;c<3;++c)packet(b,c==2?std::vector<uint32_t>(6,damage==7?maximum+1:0):std::vector<uint32_t>{damage==8?maximum+1:0,maximum,0,maximum,maximum/2,maximum/2});
 b.u32(0);return b;
}
JtDecode_VertexData::Decoded::Mover decode(const Fixture::Bytes& b,unsigned major,unsigned minor){Reader r(b,major,minor);auto decoder=JtDecode_VertexData::LoadCompressedCoords(r);check(decoder.operator bool(),"Cannot load coordinate packet");JtDecode_VertexData::Decoded result(decoder->Decode());check(r.pos==size_t(b.data.size()),"Coordinate packet envelope mismatch");return result.Move();}
}
void coordinateTests(){
 for(unsigned major:{9u,10u})for(unsigned minor:major==9?std::vector<unsigned>{0,5}:std::vector<unsigned>{0,3,5,7,8,9,10})for(bool big:{false,true}){
  for(unsigned width:{1u,3u,8u,24u,31u,32u}){
   const auto b=stream(big,width);JtDecode_VertexData::Decoded d(decode(b,major,minor));
   const auto max=uint32_t((uint64_t(1)<<width)-1);
   check(d.Count()==6&&d.CompCount()==3,"Coordinate array layout changed");
   for(int c=0;c<2;++c){check(d[0][c]==-2&&d[1][c]==5&&d[2][c]==-2&&d[3][c]==5,"Rounded coordinate endpoints changed");check(d[4][c]==float(-2.0+7.0*double(max/2)/double(max)),"Rounded coordinate interior changed");}
   for(int i=0;i<6;++i)check(d[i][2]==3,"Constant coordinate axis changed");
  }
  const auto b=stream(big,3);
  for(qsizetype size=0;size<b.data.size();++size){auto truncated=b;truncated.data.truncate(size);reject([&]{decode(truncated,major,minor);});}
  for(unsigned damage=1;damage<=8;++damage)reject([&]{decode(stream(big,3,damage),major,minor);});
  {Reader r(b,major,minor);auto decoder=JtDecode_VertexData::LoadCompressedCoords(r);bool stopped=false;JtData_Limits::Scope scope([]{throw Jt::Cancelled();});try{decoder->Decode();}catch(const Jt::Cancelled&){stopped=true;}check(stopped,"Coordinate decoding ignored cancellation");}
  {JtData_Limits::Scope scope({},0);reject([&]{decode(b,major,minor);});}
 }
 std::cout<<"9.x/10.x rounded coordinate endpoints/interiors, 1-32 bit fields, constant axes, endian/bounds/truncation and cancellation passed\n";
}
