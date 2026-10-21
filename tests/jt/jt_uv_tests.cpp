// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original UV packets and seam expectations, no downloaded geometry.
#include "JtContainerFixture.h"
#include "Jt/JtImporter.h"
#include <JtDecode_VertexUV.hxx>
#include <JtElement_ShapeLOD_Vertex.hxx>
#include <JtData_Limits.hxx>
#include <iostream>
#include <limits>
#include <vector>
namespace {
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
template<class F>void reject(F f){try{f();}catch(const Jt::Cancelled&){throw;}catch(const std::exception&){return;}throw std::runtime_error("Invalid UV packet accepted");}
struct UVTestModel:JtData_Model {UVTestModel(bool big,unsigned major,unsigned minor):JtData_Model(TCollection_ExtendedString()){myMajorVersion=major;myMinorVersion=minor;myIsFileLE=!big;}};
struct Reader:JtData_Reader {
 QByteArray bytes;size_t pos=0;Reader(const Fixture::Bytes& b,unsigned major,unsigned minor):JtData_Reader(new UVTestModel(b.big,major,minor)),bytes(b.data){}
 Standard_Boolean ReadBytes(void* p,Standard_Size n)override{if(n>size_t(bytes.size())-pos)return false;if(n)std::memcpy(p,bytes.constData()+pos,n);pos+=n;return true;}
 Standard_Boolean SkipBytes(Standard_Size n)override{if(n>size_t(bytes.size())-pos)return false;pos+=n;return true;}
 Standard_Size GetPosition()const override{return pos;}
};
void packet(Fixture::Bytes& b,const std::vector<uint32_t>& v,bool lag){b.u32(unsigned(v.size()));b.u8(0);b.u32(unsigned(v.size()*32));for(size_t i=0;i<v.size();++i)b.u32(lag&&i>=4?v[i]-v[i-1]:v[i]);}
Fixture::Bytes stream(bool big,unsigned major,bool quantized,unsigned damage=0){
 Fixture::Bytes b(big);b.u32(6);b.u8(damage==1?3:2);b.u8(damage==2?25:quantized?3:0);
 if(quantized)for(unsigned c=0;c<2;++c){b.f32(damage==11?std::numeric_limits<float>::infinity():damage==3?2.f:-2.f);b.f32(damage==3?-2.f:5.f);b.u8(damage==4?25:damage==5?0:3);}
 for(unsigned c=0;c<2;++c){std::vector<uint32_t> values,mantissae;
  for(unsigned i=0;i<6;++i){float v=float(c?5-int(i):int(i)-2);uint32_t w;std::memcpy(&w,&v,4);
   if(damage==7&&i==0)w=0x7fc00000;
   values.push_back(quantized?(damage==6?8:c?7-i:i):major==9?w>>23:w);mantissae.push_back(w&0x7fffff);}
  if(damage==8&&major==9&&!quantized)values[0]=0x200;
  if(damage==9&&major==9&&!quantized)mantissae[0]=0x800000;
  if(damage==10)values.resize(5);
  packet(b,values,quantized||major>=10);if(!quantized&&major==9)packet(b,mantissae,false);
 }
 b.u32(0);return b;
}
struct Shape:JtElement_ShapeLOD_Vertex {using JtElement_ShapeLOD_Vertex::assembleVertexRecords;};
void seams(){
 using Data=JtElement_ShapeLOD_Vertex::VertexData;using Indices=JtElement_ShapeLOD_Vertex::IndicesVec;
 Data coords(3,3),normals(4,3),colours(4,3),uv(4,2),none;
 for(int i=0;i<3;++i)for(int c=0;c<3;++c)coords[i][c]=float(i==c);
 for(int i=0;i<4;++i){normals[i][0]=normals[i][1]=0;normals[i][2]=1;for(int c=0;c<3;++c)colours[i][c]=0.5f;uv[i][0]=float(i);uv[i][1]=-float(i);}
 Indices vertices(6),attrs(6);const int v[]={0,1,2,0,2,1},a[]={0,1,2,3,2,1};
 for(int i=0;i<6;++i){vertices[i]=v[i];attrs[i]=a[i];}
 for(bool withNormals:{false,true})for(bool withColours:{false,true}){
  Shape shape;shape.assembleVertexRecords(coords,withNormals?normals:none,withColours?colours:none,uv,vertices,attrs);
  check(shape.Vertices().Count()==4&&shape.Indices()[0]!=shape.Indices()[3],"UV-only seam was merged");
  const auto first=shape.Indices()[0],second=shape.Indices()[3];
  check(shape.Vertices()[first][0]==shape.Vertices()[second][0]&&shape.TexCoords()[first][0]==0&&shape.TexCoords()[second][0]==3,"UV corner mapping changed");
 }
 Shape shape;attrs[0]=4;reject([&]{shape.assembleVertexRecords(coords,none,none,uv,vertices,attrs);});attrs[0]=0;
 Data shortUV(3,2);reject([&]{shape.assembleVertexRecords(coords,normals,none,shortUV,vertices,attrs);});
}
}
void uvTests(){
 for(unsigned major:{9u,10u})for(unsigned minor:major==9?std::vector<unsigned>{0,5}:std::vector<unsigned>{0,3,5,7,8,9,10})for(bool big:{false,true})for(bool quantized:{false,true}){
  const auto b=stream(big,major,quantized);Reader r(b,major,minor);JtDecode_VertexData::Decoded d(JtDecode_VertexUV::Read(r,2,6));
  check(r.pos==size_t(b.data.size())&&d.Count()==6&&d.CompCount()==2,"UV packet envelope mismatch");
  for(int i=0;i<6;++i){check(d[i][0]==float(i-2)&&d[i][1]==float(5-i),"UV ranges, predictor or component order changed");}
  for(qsizetype n=0;n<b.data.size();++n){auto bad=b;bad.data.truncate(n);reject([&]{Reader r(bad,major,minor);JtDecode_VertexUV::Read(r,2,6);});}
  for(unsigned damage:{1u,2u,3u,4u,5u,6u,7u,8u,9u,10u,11u}){
   if((!quantized&&damage>=3&&damage<=6)||(quantized&&damage>=7&&damage<=9)||(!quantized&&damage==11)||(major!=9&&(damage==8||damage==9)))continue;
   reject([&]{Reader r(stream(big,major,quantized,damage),major,minor);JtDecode_VertexUV::Read(r,2,6);});
  }
  reject([&]{Reader r(b,major,minor);JtDecode_VertexUV::Read(r,2,5);});
  reject([&]{Reader r(b,major,minor);JtDecode_VertexUV::Read(r,3,6);});
  {bool cancelled=false;JtData_Limits::Scope scope([]{throw Jt::Cancelled();});try{Reader r(b,major,minor);JtDecode_VertexUV::Read(r,2,6);}catch(const Jt::Cancelled&){cancelled=true;}check(cancelled,"UV decode ignored cancellation");}
  {JtData_Limits::Scope scope({},16);reject([&]{Reader r(b,major,minor);JtDecode_VertexUV::Read(r,2,6);});}
 }
 for(bool big:{false,true}){
  Fixture::Bytes b(big);b.u32(6);b.u8(2);b.u8(24);
  for(float value:{-3.f,2.f}){b.f32(value);b.f32(value);b.u8(0);}
  packet(b,std::vector<uint32_t>(6,0),true);packet(b,std::vector<uint32_t>(6,0),true);b.u32(0);
  Reader r(b,10,0);JtDecode_VertexData::Decoded d(JtDecode_VertexUV::Read(r,2,6));check(d[5][0]==-3&&d[5][1]==2,"Constant zero-bit UV changed");
  Fixture::Bytes maximum(big);maximum.u32(6);maximum.u8(2);maximum.u8(24);
  for(unsigned c=0;c<2;++c){maximum.f32(-1);maximum.f32(1);maximum.u8(24);}
  for(unsigned c=0;c<2;++c)packet(maximum,{0,0xffffff,0,0xffffff,0,0xffffff},true);maximum.u32(0);
  Reader m(maximum,10,0);JtDecode_VertexData::Decoded full(JtDecode_VertexUV::Read(m,2,6));check(full[4][0]==-1&&full[5][1]==1,"24-bit UV endpoints changed");
  for(unsigned channel:{0u,1u,7u}){Fixture::Bytes b(big);b.u64(uint64_t(3)<<(8+4*channel));Reader r(b,10,0);JtElement_ShapeLOD_Vertex::VertexBinding2 binding;check(binding.Read(r),"UV binding read failed");reject([&]{binding.NbTextCoordComponents(channel);});}
 }
 seams();std::cout<<"9.x/10.x UV transports, endian/predictor/range bounds, cancellation and UV-only seams passed\n";
}
