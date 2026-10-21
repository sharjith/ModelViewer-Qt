// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original JT 9.x exponent/mantissa and component-array vectors.
#include "JtContainerFixture.h"
#include "Jt/JtImporter.h"
#include <JtDecode_VertexColors.hxx>
#include <JtData_Limits.hxx>
#include <iostream>
#include <vector>
namespace {
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
template<class F>void reject(F f){try{f();}catch(const Jt::Cancelled&){throw;}catch(const std::exception&){return;}throw std::runtime_error("Invalid JT 9.x colour stream accepted");}
struct ColourModel:JtData_Model {ColourModel(bool big,unsigned minor):JtData_Model(TCollection_ExtendedString()){myMajorVersion=9;myMinorVersion=minor;myIsFileLE=!big;}};
struct Reader:JtData_Reader {
 QByteArray bytes;size_t pos=0;Reader(const Fixture::Bytes& b,unsigned minor):JtData_Reader(new ColourModel(b.big,minor)),bytes(b.data){}
 Standard_Boolean ReadBytes(void* p,Standard_Size n)override{if(n>size_t(bytes.size())-pos)return false;if(n)std::memcpy(p,bytes.constData()+pos,n);pos+=n;return true;}
 Standard_Boolean SkipBytes(Standard_Size n)override{if(n>size_t(bytes.size())-pos)return false;pos+=n;return true;}
 Standard_Size GetPosition()const override{return pos;}
};
void packet(Fixture::Bytes& b,const std::vector<uint32_t>& values,bool lag){b.u32(unsigned(values.size()));if(values.empty())return;b.u8(0);b.u32(unsigned(values.size()*32));for(size_t i=0;i<values.size();++i)b.u32(lag&&i>=4?values[i]-values[i-1]:values[i]);}
Fixture::Bytes stream(bool big,unsigned components,unsigned mode,unsigned damage=0){
 Fixture::Bytes b(big);b.u32(6);b.u8(components);b.u8(mode?8:0);
 if(mode==0){
  for(unsigned c=0;c<components;++c){std::vector<uint32_t> exp,mant;for(unsigned i=0;i<6;++i){
   float value=float((i+c)%6)/5;uint32_t word;std::memcpy(&word,&value,4);
   if(damage==6&&i==0)word=0x40000000u;if(damage==7&&i==0)word=0xbf800000u;if(damage==8&&i==0)word=0x7fc00000u;
   exp.push_back(damage==1&&i==0?512:word>>23);mant.push_back(damage==2&&i==0?0x800000u:word&0x7fffffu);
  }packet(b,exp,false);packet(b,mant,false);}
 }else{
  const bool hsv=mode==2;b.u8(hsv?1:0);
  for(unsigned c=0;c<4;++c){if(!hsv){b.f32(c==3&&components==3?1.f:0.f);b.f32(1.f);}b.u8(c==3&&components==3?0:hsv&&c==0?2:8);}
  for(unsigned c=0;c<4;++c){std::vector<uint32_t> values;
   for(unsigned i=0;i<(damage==3&&c==3?5u:6u);++i){
    uint32_t v=c==3?(components==3?0:255):hsv?(c==0?i%4:255):i%3==c?255:0;
    if(damage==4&&c==0&&i==4)v=256;if(damage==5&&c==3&&i==0)v=1;
    values.push_back(v);
   }packet(b,values,true);
  }
 }
 b.u32(0);return b;
}
}
void jt9ColourTests(){
 for(unsigned minor:{0u,5u})for(bool big:{false,true})for(unsigned components:{3u,4u})for(unsigned mode:{0u,1u,2u}){
  auto b=stream(big,components,mode);Reader r(b,minor);JtDecode_VertexData::Decoded d(JtDecode_VertexColors::Read(r,components,6));
  check(r.pos==size_t(b.data.size())&&d.Count()==6&&d.CompCount()==int(components),"JT 9.x colour envelope mismatch");
  for(unsigned i=0;i<6;++i)for(unsigned c=0;c<components;++c){
   const unsigned channel=mode==2?(i%4==3?0:i%4):i%3;
   const float expected=mode==0?float((i+c)%6)/5:c==3?1.f:float(channel==c);
   check(std::abs(d[int(i)][int(c)]-expected)<1e-6f,"JT 9.x colour exponent/mantissa, Lag1 or channel mapping mismatch");
  }
  for(qsizetype n=0;n<b.data.size();++n){auto shortData=b;shortData.data.truncate(n);reject([&]{Reader r(shortData,minor);JtDecode_VertexColors::Read(r,components,6);});}
  if(mode==0){for(unsigned damage:{1u,2u,6u,7u,8u})reject([&]{Reader r(stream(big,components,mode,damage),minor);JtDecode_VertexColors::Read(r,components,6);});}
  else {
   reject([&]{Reader r(stream(big,components,mode,3),minor);JtDecode_VertexColors::Read(r,components,6);});
   reject([&]{Reader r(stream(big,components,mode,4),minor);JtDecode_VertexColors::Read(r,components,6);});
   if(components==3)reject([&]{Reader r(stream(big,components,mode,5),minor);JtDecode_VertexColors::Read(r,components,6);});
  }
  reject([&]{Reader r(b,minor);JtDecode_VertexColors::Read(r,components,5);});
 }
 {Reader r(stream(false,4,0),5);bool cancelled=false;JtData_Limits::Scope scope([]{throw Jt::Cancelled();});try{JtDecode_VertexColors::Read(r,4,6);}catch(const Jt::Cancelled&){cancelled=true;}check(cancelled,"JT 9.x colours ignored cancellation");}
 {Reader r(stream(false,4,1),5);JtData_Limits::Scope scope({},32);reject([&]{JtDecode_VertexColors::Read(r,4,6);});}
 std::cout<<"JT 9.0/9.5 LE/BE lossless exponent/mantissa and four-vector quantized RGB/HSV colours passed\n";
}
