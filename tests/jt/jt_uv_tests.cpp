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
Fixture::Bytes stream(bool big,unsigned major,bool quantized,unsigned damage=0,unsigned components=2){
 Fixture::Bytes b(big);b.u32(6);b.u8(damage==1?3:components);b.u8(damage==2?25:quantized?3:0);
 if(quantized)for(unsigned c=0;c<components;++c){b.f32(damage==11?std::numeric_limits<float>::infinity():damage==3?2.f:-2.f);b.f32(damage==3?-2.f:5.f);b.u8(damage==4?25:damage==5?0:3);}
 for(unsigned c=0;c<components;++c){std::vector<uint32_t> values,mantissae;
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
 Data coords(3,3),normals(4,3),colours(4,3),none;
 JtElement_ShapeLOD_Vertex::TextureData channels;channels[0].Allocate(4,2);auto& uv=channels[0];
 for(int i=0;i<3;++i)for(int c=0;c<3;++c)coords[i][c]=float(i==c);
 for(int i=0;i<4;++i){normals[i][0]=normals[i][1]=0;normals[i][2]=1;for(int c=0;c<3;++c)colours[i][c]=0.5f;uv[i][0]=float(i);uv[i][1]=-float(i);}
 Indices vertices(6),attrs(6);const int v[]={0,1,2,0,2,1},a[]={0,1,2,3,2,1};
 for(int i=0;i<6;++i){vertices[i]=v[i];attrs[i]=a[i];}
 for(bool withNormals:{false,true})for(bool withColours:{false,true}){
  // Only set 3 differs at the shared position; sets 0-2 remain constant.
  for(unsigned channel=1;channel<4;++channel){channels[channel].Allocate(4,2);for(int i=0;i<4;++i){channels[channel][i][0]=channel==3?float(i):0;channels[channel][i][1]=float(channel);}}
  for(int i=0;i<4;++i)uv[i][0]=uv[i][1]=0;
  Shape shape;shape.assembleVertexRecords(coords,withNormals?normals:none,withColours?colours:none,channels,vertices,attrs);
  check(shape.Vertices().Count()==4&&shape.Indices()[0]!=shape.Indices()[3],"UV-only seam was merged");
  const auto first=shape.Indices()[0],second=shape.Indices()[3];
  check(shape.Vertices()[first][0]==shape.Vertices()[second][0]&&shape.TexCoords(3)[first][0]==0&&shape.TexCoords(3)[second][0]==3,"UV corner mapping changed");
 }
 Shape shape;attrs[0]=4;reject([&]{shape.assembleVertexRecords(coords,none,none,channels,vertices,attrs);});attrs[0]=0;
 for(unsigned channel=0;channel<3;++channel)channels[channel].Free();
 for(bool withNormals:{false,true}) {
  shape.assembleVertexRecords(coords,withNormals?normals:none,none,channels,vertices,attrs);
  check(shape.Vertices().Count()==4&&shape.Indices()[0]!=shape.Indices()[3]&&!shape.TexCoords(0).Count()&&shape.TexCoords(3).Count()==4,"Sparse UV-only seam or absent source slot changed");
 }
 channels[3].Allocate(3,2);reject([&]{shape.assembleVertexRecords(coords,normals,none,channels,vertices,attrs);});
 JtElement_ShapeLOD_Vertex::TextureData shortUV;shortUV[0].Allocate(3,2);reject([&]{shape.assembleVertexRecords(coords,normals,none,shortUV,vertices,attrs);});
}
}
void uvTests(){
 for(unsigned major:{9u,10u})for(unsigned minor:major==9?std::vector<unsigned>{0,5}:std::vector<unsigned>{0,3,5,7,8,9,10})for(bool big:{false,true})for(bool quantized:{false,true}){
  for(unsigned sets=0;sets<=4;++sets){Fixture::Bytes bindingBytes(big);uint64_t flags=0;for(unsigned c=0;c<sets;++c)flags|=uint64_t(0x200)<<(4*c);bindingBytes.u64(flags);Reader r(bindingBytes,major,minor);JtElement_ShapeLOD_Vertex::VertexBinding2 binding;check(binding.Read(r)&&binding.TextureChannelCount()==sets,"Multi-UV binding/endian count changed");}
  for(uint64_t flags:{uint64_t(0x2222200),uint64_t(0x400),uint64_t(0x800)}){Fixture::Bytes b(big);b.u64(flags);Reader r(b,major,minor);JtElement_ShapeLOD_Vertex::VertexBinding2 binding;check(binding.Read(r),"UV negative binding read failed");reject([&]{binding.TextureChannelCount();});}
  for(unsigned mask=0;mask<16;++mask){Fixture::Bytes b(big);uint64_t flags=0;unsigned count=0;
   for(unsigned c=0;c<4;++c)if(mask&(1u<<c)){flags|=uint64_t(0x200)<<(4*c);++count;}
   b.u64(flags);Reader r(b,major,minor);JtElement_ShapeLOD_Vertex::VertexBinding2 binding;
   check(binding.Read(r)&&binding.TextureChannelCount()==count,"Sparse UV set count/endian changed");
   for(unsigned c=0;c<4;++c)check(binding.NbTextCoordComponents(c)==((mask&(1u<<c))?2:0),"Sparse UV set identity changed");
  }
  {const auto scalar=stream(big,major,quantized,0,1);Reader r(scalar,major,minor);JtDecode_VertexData::Decoded d(JtDecode_VertexUV::Read(r,1,6));check(d.CompCount()==1&&r.pos==size_t(scalar.data.size()),"Scalar UV envelope changed");for(int i=0;i<6;++i)check(d[i][0]==float(i-2),"Scalar U value changed");for(qsizetype size=0;size<scalar.data.size();++size){auto shortData=scalar;shortData.data.truncate(size);reject([&]{Reader r(shortData,major,minor);JtDecode_VertexUV::Read(r,1,6);});}}
  {Fixture::Bytes b(big);b.u64(0x212100);Reader r(b,major,minor);JtElement_ShapeLOD_Vertex::VertexBinding2 binding;check(binding.Read(r)&&binding.TextureChannelCount()==4&&binding.NbTextCoordComponents(0)==1&&binding.NbTextCoordComponents(1)==2,"Mixed scalar/UV binding changed");}
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
  // Original literal controls for the hash-verified unpredicted IEEE profile.
  // Golden lookup2 component-array hashes were calculated independently of
  // the C++ reader. Preserve the existing Lag1 form with the same values/hash.
  for(unsigned components:{1u,2u})for(bool predicted:{false,true}) {
   const float expected[2][6]={{-2,-1,0,.25f,.5f,1},{2,1,.5f,.25f,0,-1}};
   Fixture::Bytes binary(big);binary.u32(6);binary.u8(components);binary.u8(0);
   for(unsigned c=0;c<components;++c) {
    std::vector<uint32_t> words;
    for(float value:expected[c]){uint32_t word;std::memcpy(&word,&value,4);words.push_back(word);}
    packet(binary,words,predicted);
   }
   binary.u32(components==1?0x45d1bab7u:0xff192c80u);
   for(unsigned minor:{0u,9u,10u}) {
    Reader reader(binary,10,minor);JtDecode_VertexData::Decoded values(JtDecode_VertexUV::Read(reader,components,6));
    check(reader.pos==size_t(binary.data.size()),"Hashed IEEE UV envelope changed");
    for(unsigned c=0;c<components;++c)for(unsigned i=0;i<6;++i)
     check(values[int(i)][int(c)]==expected[c][i],"Hashed raw/Lag1 UV profile changed");
   }
  }
  {
   Fixture::Bytes invalid(big);invalid.u32(6);invalid.u8(1);invalid.u8(0);
   packet(invalid,{0x7fc00000,0,0,0,0,0},false);invalid.u32(0x6b92ca1a);
   reject([&]{Reader r(invalid,10,9);JtDecode_VertexUV::Read(r,1,6);});
  }
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
