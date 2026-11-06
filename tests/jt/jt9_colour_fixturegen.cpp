// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original private fixture modifier; source geometry is never bundled.
#include "JtContainerFixture.h"
#include "Jt/JtImporter.h"
#include <JtDecode_MeshCoderDriver.hxx>
#include <JtDecode_VertexData.hxx>
#include <QCoreApplication>
#include <QDir>
#include <lzma.h>
#include <zlib.h>
#include <iostream>
#include <vector>
namespace {
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
struct ColourModel:JtData_Model {ColourModel(unsigned minor):JtData_Model(TCollection_ExtendedString()){myMajorVersion=9;myMinorVersion=minor;myIsFileLE=true;}};
struct Reader:JtData_Reader {
 QByteArray bytes;size_t pos=0;Reader(QByteArray b,unsigned minor):JtData_Reader(new ColourModel(minor)),bytes(std::move(b)){}
 Standard_Boolean ReadBytes(void* p,Standard_Size n)override{if(n>size_t(bytes.size())-pos)return false;if(n)std::memcpy(p,bytes.constData()+pos,n);pos+=n;return true;}
 Standard_Boolean SkipBytes(Standard_Size n)override{if(n>size_t(bytes.size())-pos)return false;pos+=n;return true;}
 Standard_Size GetPosition()const override{return pos;}
};
quint64 number(const QByteArray& b,qsizetype at,unsigned size){check(at>=0&&at+size<=b.size(),"Fixture read bounds");quint64 v=0;for(unsigned i=0;i<size;++i)v|=quint64(quint8(b[at+i]))<<(8*i);return v;}
void put(QByteArray& b,qsizetype at,quint64 value,unsigned size){check(at>=0&&at+size<=b.size(),"Fixture write bounds");for(unsigned i=0;i<size;++i)b[at+i]=char(value>>(8*i));}
QByteArray graphSegment(const QByteArray& source,unsigned components){
 check(source.size()>=33,"Short graph segment");const auto flag=number(source,24,4),n=number(source,28,4),algo=number(source,32,1);
 check(n>=1&&32+n==quint64(source.size()),"Graph envelope mismatch");QByteArray data;
 if(flag==0)data=source.mid(33);
 else {
  std::vector<uint8_t> buffer(64*1024*1024);size_t size=buffer.size();
  if(algo==2){uLongf out=uLongf(size);check(uncompress(buffer.data(),&out,reinterpret_cast<const Bytef*>(source.constData()+33),uLong(n-1))==Z_OK,"Cannot inflate source graph");size=out;}
  else {check(algo==3,"Unsupported fixture graph compression");size_t in=0,out=0;uint64_t memory=128*1024*1024;
   check(lzma_stream_buffer_decode(&memory,0,nullptr,reinterpret_cast<const uint8_t*>(source.constData()+33),&in,size_t(n-1),buffer.data(),&out,buffer.size())==LZMA_OK&&in==n-1,"Cannot unpack source graph");size=out;}
  data=QByteArray(reinterpret_cast<const char*>(buffer.data()),qsizetype(size));
 }
 unsigned shapes=0;qsizetype pos=0;
 while(pos+4<=data.size()){
  const auto n=number(data,pos,4);if(n==16)break;check(n>=21&&pos+4+n<=quint64(data.size()),"Graph record bounds");
  if(number(data,pos+4,4)==0x10dd1077){
   const qsizetype end=pos+4+qsizetype(n);std::vector<qsizetype> bindings;
   if(number(data,end-22,2)==2)bindings={end-20,end-8};
   else {check(number(data,end-14,2)==1,"Unqualified source vertex-node local version");bindings={end-12};}
   for(auto at:bindings)put(data,at,(number(data,at,8)&~quint64(0x30))|(components==3?0x10:0x20),8);++shapes;
  }pos+=4+qsizetype(n);
 }
 check(shapes>0,"No source triangle-strip node");Fixture::Bytes envelope;envelope.u32(0);envelope.u32(unsigned(data.size()+1));envelope.u8(0);envelope.data+=data;
 auto result=source.left(24)+envelope.data;put(result,20,result.size(),4);return result;
}
void packet(Fixture::Bytes& b,const std::vector<uint32_t>& v,bool lag){b.u32(unsigned(v.size()));if(v.empty())return;b.u8(0);b.u32(unsigned(v.size()*32));for(size_t i=0;i<v.size();++i)b.u32(lag&&i>=4?v[i]-v[i-1]:v[i]);}
QByteArray shapeSegment(const QByteArray& source,unsigned minor,unsigned mode){
 auto data=source.mid(24);check(number(data,4,4)==0x10dd10ab,"Fixture needs TriStrip LOD");Reader reader(data,minor);check(reader.SkipBytes(25),"Short shape header");Jt_I16 v;Jt_U64 binding;
 check(reader.ReadVersion(v)&&reader.ReadVersion(v),"Short shape versions");const auto outer=reader.pos;check(reader.ReadU64(binding)&&(binding&0x30)==0,"Source already has colours");
 Jt_I32 id;check(reader.ReadVersion(v)&&reader.ReadI32(id)&&reader.ReadVersion(v),"Short mesh versions");
 auto topology=JtDecode_MeshCoderDriver::LoadInputData(reader);check(topology.operator bool(),"Cannot scan source topology");
 const auto inner=reader.pos;check(reader.ReadU64(binding)&&(binding&0x30)==0,"Source inner binding already coloured");check(reader.SkipBytes(4),"Short quantization parameters");Jt_I32 vertices,attributes;
 check(reader.ReadI32(vertices)&&reader.ReadI32(attributes)&&vertices>0&&attributes>0&&attributes<1000000,"Invalid fixture vertex count");
 auto coords=JtDecode_VertexData::LoadCompressedCoords(reader),normals=JtDecode_VertexData::LoadCompressedNormals(reader);
 check(coords.operator bool()&&normals.operator bool()&&normals->GetOutVertexCount()==attributes,"Cannot scan source vertex arrays");
 const unsigned components=mode==1||mode==3?4:3;const bool quantized=mode>=2,hsv=mode==4;Fixture::Bytes colours;colours.u32(attributes);colours.u8(components);colours.u8(quantized?8:0);
 if(!quantized){
  for(unsigned c=0;c<components;++c){std::vector<uint32_t> exp,mant;for(int i=0;i<attributes;++i){float value=c==3?1.f:float(unsigned(i/10)%3==c);uint32_t word;std::memcpy(&word,&value,4);exp.push_back(word>>23);mant.push_back(word&0x7fffff);}packet(colours,exp,false);packet(colours,mant,false);}
 }else{
  colours.u8(hsv?1:0);for(unsigned c=0;c<4;++c){if(!hsv){colours.f32(c==3&&components==3?1.f:0.f);colours.f32(1.f);}colours.u8(c==3&&components==3?0:hsv&&c==0?2:8);}
  for(unsigned c=0;c<4;++c){std::vector<uint32_t> values;for(int i=0;i<attributes;++i){const auto channel=unsigned(i/10)%3;values.push_back(c==3?(components==3?0:255):hsv?(c==0?channel:255):channel==c?255:0);}packet(colours,values,true);}
 }
 colours.u32(0);data.insert(qsizetype(reader.pos),colours.data);
 for(auto at:{outer,inner})put(data,qsizetype(at),(number(data,qsizetype(at),8)&~quint64(0x30))|(components==3?0x10:0x20),8);
 put(data,0,number(data,0,4)+quint64(colours.data.size()),4);auto result=source.left(24)+data;put(result,20,result.size(),4);return result;
}
QByteArray fixture(const QByteArray& source,unsigned minor,unsigned mode){
 check(source.startsWith("Version 9.")&&number(source,80,1)==0,"Fixture needs little-endian JT 9.x");auto output=source;const auto toc=number(source,85,4),count=number(source,qsizetype(toc),4);check(count>0&&count<10000,"Bad source TOC");unsigned shapes=0;
 for(quint64 i=0;i<count;++i){const qsizetype entry=qsizetype(toc+4+i*28);const auto off=number(source,entry+16,4),n=number(source,entry+20,4),type=number(source,entry+24,4)>>24;check(off+n<=quint64(source.size()),"Source segment bounds");QByteArray segment;
  if(type==1)segment=graphSegment(source.mid(qsizetype(off),qsizetype(n)),mode==1||mode==3?4:3);
  else if(type>=7&&type<=16){segment=shapeSegment(source.mid(qsizetype(off),qsizetype(n)),minor,mode);++shapes;}else continue;
  check(quint64(output.size())+quint64(segment.size())<0xffffffffu,"Fixture output exceeds 32-bit offsets");put(output,entry+16,output.size(),4);put(output,entry+20,segment.size(),4);output+=segment;
 }check(shapes>0,"No display shape");return output;
}
void save(const QString& path,const QByteArray& data){QFile f(path);check(f.open(QIODevice::WriteOnly)&&f.write(data)==data.size(),"Cannot write private fixture");}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
 check(argc==3,"Usage: jt9_colour_fixturegen <local-single-part-9.x.jt> <output-folder>");const auto input=QString::fromLocal8Bit(argv[1]);QFile f(input);check(f.open(QIODevice::ReadOnly),"Cannot open local source");const auto source=f.readAll();
 unsigned minor;if(source.startsWith("Version 9.0 "))minor=0;else {check(source.startsWith("Version 9.5 "),"Unqualified fixture source minor version");minor=5;}
 auto baseline=Jt::Importer::read(input);check(baseline.scene->mNumMeshes==1,"Fixture needs one source mesh");const auto& original=*baseline.scene->mMeshes[0];QDir folder(QString::fromLocal8Bit(argv[2]));check(QDir().mkpath(folder.path()),"Cannot create fixture directory");
 const char* names[]={"lossless_rgb.jt","lossless_rgba_opaque.jt","quantized_rgb.jt","quantized_rgba_opaque.jt","quantized_hsv.jt"};
 for(unsigned mode=0;mode<5;++mode){const auto path=folder.filePath(names[mode]);save(path,fixture(source,minor,mode));auto result=Jt::Importer::read(path);check(result.scene->mNumMeshes==1,"Fixture changed mesh count");const auto& mesh=*result.scene->mMeshes[0];
  check(mesh.mNumVertices==original.mNumVertices&&mesh.mNumFaces==original.mNumFaces&&mesh.HasVertexColors(0),"Fixture geometry count/colour binding changed");
  for(unsigned i=0;i<mesh.mNumVertices;++i)check(mesh.mVertices[i]==original.mVertices[i]&&mesh.mNormals[i]==original.mNormals[i],"Fixture changed coordinates/normals");
  for(unsigned i=0;i<mesh.mNumFaces;++i)for(unsigned c=0;c<3;++c)check(mesh.mFaces[i].mIndices[c]==original.mFaces[i].mIndices[c],"Fixture changed triangle indices");
  if(mode){auto reference=Jt::Importer::read(folder.filePath(names[0]));for(unsigned i=0;i<mesh.mNumVertices;++i)check(mesh.mColors[0][i]==reference.scene->mMeshes[0]->mColors[0][i],"Colour transports disagree with lossless reference");}
 }
 save(folder.filePath("README.txt"),"Private source-derived controls, not redistributable by inference. All five should show identical red/green/blue patches. Geometry, normals and indices are checked against the original source. Colour hashes are placeholders, not independently verified conformance.\n");
 std::cout<<"JT 9."<<minor<<" private lossless/RGBA/quantized RGB/HSV controls passed; vertices="<<original.mNumVertices<<" triangles="<<original.mNumFaces<<'\n';return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
