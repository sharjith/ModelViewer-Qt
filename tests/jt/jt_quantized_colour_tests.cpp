// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original packed RGB/HSV reference vectors, no downloaded geometry.
#include "JtContainerFixture.h"
#include "Jt/JtImporter.h"
#include <JtDecode_VertexColors.hxx>
#include <JtData_Limits.hxx>
#include <iostream>
#include <vector>
namespace {
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
template<class F>void reject(F f){try{f();}catch(const Jt::Cancelled&){throw;}catch(const std::exception&){return;}throw std::runtime_error("Invalid quantized colour packet accepted");}
struct ColourModel:JtData_Model {ColourModel(bool big,unsigned minor,unsigned major=10):JtData_Model(TCollection_ExtendedString()){myMajorVersion=major;myMinorVersion=minor;myIsFileLE=!big;}};
struct Reader:JtData_Reader {
 QByteArray bytes;size_t pos=0;Reader(const Fixture::Bytes& b,unsigned minor=0,unsigned major=10):JtData_Reader(new ColourModel(b.big,minor,major)),bytes(b.data){}
 Standard_Boolean ReadBytes(void* p,Standard_Size n)override{if(n>size_t(bytes.size())-pos)return false;if(n)std::memcpy(p,bytes.constData()+pos,n);pos+=n;return true;}
 Standard_Boolean SkipBytes(Standard_Size n)override{if(n>size_t(bytes.size())-pos)return false;pos+=n;return true;}
 Standard_Size GetPosition()const override{return pos;}
};
Fixture::Bytes stream(bool big,unsigned components,bool hsv,unsigned damage=0){
 Fixture::Bytes b(big);b.u32(6);b.u8(components);b.u8(damage==1?9:8);b.u8(damage==2?2:unsigned(hsv));
 for(unsigned c=0;c<4;++c){
  const unsigned width=hsv?(c==0?(damage==8?3:2):c==3&&components==3?0:8):(c==3&&components==3?0:8);
  if(!hsv){b.f32(damage==3&&c==0?0.9f:c==3&&components==3?1.f:0.f);b.f32(damage==3&&c==0?0.1f:1.f);}
  b.u8(damage==4&&c==0?9:(damage==5||(damage==3&&hsv))&&c==0?0:width);
 }
 b.u32(damage==6?5:6);b.u8(0);b.u32((damage==6?5:6)*32);
 for(unsigned i=0;i<(damage==6?5u:6u);++i){
  uint32_t code=hsv?((damage==8?i+1:i%3)<<16)|0xffffu:(i%3==0?0xff0000u:i%3==1?0xff00u:0xffu);
  if(components==4)code=(code<<8)|(damage==9?i*51:255);
  if(damage==7&&components==3)code|=0x80000000u;
  b.u32(code);
 }
 b.u32(0);return b;
}
void verify(const Fixture::Bytes& b,unsigned components,unsigned minor){
 Reader r(b,minor);JtDecode_VertexData::Decoded d(JtDecode_VertexColors::Read(r,components,6));
 check(r.pos==size_t(b.data.size())&&d.Count()==6&&d.CompCount()==int(components),"Quantized colour envelope changed");
 for(unsigned i=0;i<6;++i)for(unsigned c=0;c<components;++c)check(std::abs(d[int(i)][int(c)]-(c==3?1.f:float(i%3==c)))<1e-6f,"Quantized RGB/HSV channels, hue or packing differ from known primaries");
}
}
void quantizedColourTests(){
 for(unsigned minor:{0u,3u,5u,7u,8u,9u,10u})for(bool big:{false,true})for(bool hsv:{false,true})for(unsigned components:{3u,4u}){
  auto b=stream(big,components,hsv);verify(b,components,minor);
  for(qsizetype n=0;n<b.data.size();++n){auto shortData=b;shortData.data.truncate(n);reject([&]{Reader r(shortData,minor);JtDecode_VertexColors::Read(r,components,6);});}
  for(unsigned damage=1;damage<=7;++damage){if(damage==7&&components==4)continue;reject([&]{Reader r(stream(big,components,hsv,damage),minor);JtDecode_VertexColors::Read(r,components,6);});}
  reject([&]{Reader r(b,minor);JtDecode_VertexColors::Read(r,components,5);});
 }
 // Two-bit RGB range endpoints and interior levels: inverse of round-to-nearest encoder.
 for(bool big:{false,true}){
  Fixture::Bytes b(big);b.u32(4);b.u8(3);b.u8(2);b.u8(0);
  for(unsigned c=0;c<4;++c){b.f32(c==3?1.f:0.f);b.f32(1.f);b.u8(c==3?0:2);}
  b.u32(4);b.u8(0);b.u32(128);for(unsigned i=0;i<4;++i)b.u32((i<<4)|(i<<2)|i);b.u32(0);
  Reader r(b);JtDecode_VertexData::Decoded d(JtDecode_VertexColors::Read(r,3,4));
  for(unsigned i=0;i<4;++i)for(unsigned c=0;c<3;++c)check(std::abs(d[int(i)][int(c)]-float(i)/3)<1e-6f,"Colour dequantization endpoints or intermediate levels differ");
  {Reader r(stream(big,3,true,8));JtDecode_VertexData::Decoded d(JtDecode_VertexColors::Read(r,3,6));
   const float expected[6][3]={{1,6.f/7,0},{2.f/7,1,0},{0,1,4.f/7},{0,4.f/7,1},{2.f/7,0,1},{1,0,6.f/7}};
   for(int i=0;i<6;++i)for(int c=0;c<3;++c)check(std::abs(d[i][c]-expected[i][c])<1e-6f,"HSV sector interpolation differs from known values");}
  {Reader r(stream(big,4,false,9));JtDecode_VertexData::Decoded d(JtDecode_VertexColors::Read(r,4,6));for(int i=0;i<6;++i)check(std::abs(d[i][3]-float(i)/5)<1e-6f,"Packed alpha values changed");}
  {auto bad=stream(big,3,false);Fixture::Bytes nan(big);nan.u32(0x7fc00000);bad.data.replace(7,4,nan.data);Reader r(bad);reject([&]{JtDecode_VertexColors::Read(r,3,6);});}
  {Reader r(stream(big,3,false));reject([&]{JtDecode_VertexColors::Read(r,2,6);});}
  for(unsigned components:{3u,4u}) {
   Fixture::Bytes b(big);b.u32(6);b.u8(components);b.u8(8);b.u8(0);
   for(unsigned c=0;c<4;++c){const float value=float(c+1)/4;b.f32(value);b.f32(value);b.u8(0);}
   b.u32(6);b.u8(0);b.u32(192);for(int i=0;i<6;++i)b.u32(0);b.u32(0);
   Reader r(b);JtDecode_VertexData::Decoded d(JtDecode_VertexColors::Read(r,components,6));
   for(int i=0;i<6;++i)for(unsigned c=0;c<components;++c)check(d[i][int(c)]==float(c+1)/4,"Constant zero-bit RGB quantizer changed");
   Fixture::Bytes word(big);word.u32(1);b.data.replace(6+1+36+9,4,word.data);Reader bad(b);reject([&]{JtDecode_VertexColors::Read(bad,components,6);});
  }
  // Hue=6 wraps to red, rather than selecting an invalid sixth sector.
  auto hue=stream(big,3,true);const auto firstCode=6+1+4+4+1+4;
  Fixture::Bytes word(big);word.u32(0x3ffffu);hue.data.replace(firstCode,4,word.data);
  Reader h(hue);JtDecode_VertexData::Decoded colours(JtDecode_VertexColors::Read(h,3,6));check(colours[0][0]==1&&colours[0][1]==0&&colours[0][2]==0,"Hue endpoint did not wrap");
 }
 for(unsigned minor:{0u,5u}){Reader r(stream(false,3,false),minor,9);reject([&]{JtDecode_VertexColors::Read(r,3,6);});}
 {auto b=stream(false,4,false);Reader r(b);bool cancelled=false;JtData_Limits::Scope scope([]{throw Jt::Cancelled();});try{JtDecode_VertexColors::Read(r,4,6);}catch(const Jt::Cancelled&){cancelled=true;}check(cancelled,"Colour decoding ignored cancellation");}
 {auto b=stream(false,4,false);Reader r(b);JtData_Limits::Scope scope({},32);reject([&]{JtDecode_VertexColors::Read(r,4,6);});}
 std::cout<<"Modern quantized RGB/HSV 10.0/10.3/10.5/10.7/10.8/10.9/10.10 LE/BE packing, ranges, bounds and cancellation passed\n";
}
