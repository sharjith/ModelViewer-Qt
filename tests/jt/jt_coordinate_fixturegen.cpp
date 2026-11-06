// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original private coordinate-quantization controls; no bundled source geometry.
// Source geometry is never bundled. Limited to observed local single-part layouts.
#include "JtContainerFixture.h"
#include "Jt/JtImporter.h"
#include <JtDecode_MeshCoderDriver.hxx>
#include <JtDecode_VertexData.hxx>
#include "SceneUtils.h"
#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
#include <QCoreApplication>
#include <QDir>
#include <lzma.h>
#include <zlib.h>
#include <iostream>
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>
#include <limits>
namespace {
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
struct CoordinateFixtureModel:JtData_Model {CoordinateFixtureModel(unsigned major,unsigned minor):JtData_Model(TCollection_ExtendedString()){myMajorVersion=major;myMinorVersion=minor;myIsFileLE=true;}};
struct Reader:JtData_Reader {
 QByteArray bytes;size_t pos=0;Reader(QByteArray b,unsigned major,unsigned minor):JtData_Reader(new CoordinateFixtureModel(major,minor)),bytes(std::move(b)){}
 Standard_Boolean ReadBytes(void* p,Standard_Size n)override{if(n>size_t(bytes.size())-pos)return false;if(n)std::memcpy(p,bytes.constData()+pos,n);pos+=n;return true;}
 Standard_Boolean SkipBytes(Standard_Size n)override{if(n>size_t(bytes.size())-pos)return false;pos+=n;return true;}
 Standard_Size GetPosition()const override{return pos;}
};
quint64 number(const QByteArray& b,qsizetype at,unsigned size){check(at>=0&&at+size<=b.size(),"Fixture read bounds");quint64 v=0;for(unsigned i=0;i<size;++i)v|=quint64(quint8(b[at+i]))<<(8*i);return v;}
void put(QByteArray& b,qsizetype at,quint64 value,unsigned size){check(at>=0&&at+size<=b.size(),"Fixture write bounds");for(unsigned i=0;i<size;++i)b[at+i]=char(value>>(8*i));}
using Ranges=std::array<std::array<float,2>,3>;
void packet(Fixture::Bytes& b,const std::vector<uint32_t>& values){b.u32(unsigned(values.size()));b.u8(0);b.u32(unsigned(values.size()*32));for(size_t i=0;i<values.size();++i)b.u32(i>=4?values[i]-values[i-1]:values[i]);}
uint32_t quantize(float value,const std::array<float,2>& range,unsigned width){
 if(range[0]==range[1])return 0;const double maximum=double((uint64_t(1)<<width)-1);
 return uint32_t(std::clamp(std::floor((double(value)-range[0])*maximum/(double(range[1])-range[0])+0.5),0.0,maximum));
}
QByteArray shapeSegment(const QByteArray& source,unsigned major,unsigned minor,unsigned width,unsigned damage,Ranges& ranges,bool collectOnly=false){
 auto data=source.mid(24);check(number(data,4,4)==0x10dd10ab,"Fixture needs TriStrip LOD");Reader reader(data,major,minor);check(reader.SkipBytes(25),"Short shape header");Jt_I16 v;Jt_U64 binding;
 check(reader.ReadVersion(v)&&reader.ReadVersion(v)&&reader.ReadU64(binding),"Short shape versions/binding");
 size_t innerLength=0;if(major>=10){innerLength=reader.pos;check(reader.SkipBytes(25),"Short inner shape envelope");}
 Jt_I32 id;check(reader.ReadVersion(v)&&reader.ReadI32(id)&&reader.ReadVersion(v),"Short mesh versions");
 auto topology=JtDecode_MeshCoderDriver::LoadInputData(reader);check(topology.operator bool(),"Cannot scan source topology");
 check(reader.ReadU64(binding),"Short inner binding");const size_t quantization=reader.pos;check(reader.SkipBytes(4),"Short quantization parameters");Jt_I32 vertices,attributes;
 check(reader.ReadI32(vertices)&&reader.ReadI32(attributes)&&vertices>0&&vertices<1000000,"Invalid fixture coordinate count");
 const auto start=reader.pos;auto decoder=JtDecode_VertexData::LoadCompressedCoords(reader);check(decoder.operator bool(),"Cannot scan source coordinate arrays");const auto end=reader.pos;
 JtDecode_VertexData::Decoded coords(decoder->Decode());check(coords.Count()==vertices&&coords.CompCount()==3,"Source coordinate dimensions changed");
 if(collectOnly){for(unsigned c=0;c<3;++c)for(int i=0;i<vertices;++i){ranges[c][0]=std::min(ranges[c][0],coords[i][c]);ranges[c][1]=std::max(ranges[c][1],coords[i][c]);}return source;}
 Fixture::Bytes replacement;replacement.u32(vertices);replacement.u8(3);
 for(unsigned c=0;c<3;++c){replacement.f32(ranges[c][0]);replacement.f32(ranges[c][1]);replacement.u8(damage==1&&c==0?33:width);}
 for(unsigned c=0;c<3;++c){std::vector<uint32_t> codes;for(int i=0;i<vertices;++i)codes.push_back(damage==2&&i==0&&c==0?uint32_t(1u<<width):quantize(coords[i][c],ranges[c],width));packet(replacement,codes);}
 replacement.u32(0);data.replace(qsizetype(start),qsizetype(end-start),replacement.data);put(data,qsizetype(quantization),width,1);
 const qint64 delta=qint64(replacement.data.size())-qint64(end-start);put(data,0,quint64(qint64(number(data,0,4))+delta),4);
 if(major>=10)put(data,qsizetype(innerLength),quint64(qint64(number(data,qsizetype(innerLength),4))+delta),4);
 auto result=source.left(24)+data;put(result,20,result.size(),4);return result;
}
QByteArray fixture(const QByteArray& source,unsigned major,unsigned minor,unsigned width,unsigned damage,Ranges& ranges){
 check(number(source,80,1)==0,"Fixture needs little-endian input");auto output=source;const unsigned offsetBytes=major>=10?8:4,entrySize=major>=10?32:28;
 const auto toc=number(source,85,offsetBytes),count=number(source,qsizetype(toc),4);check(count>0&&count<10000,"Bad source TOC");unsigned shapes=0;
 // Use one common range over every stored LOD, then rewrite each collection.
 // This keeps the expected transform identical whichever alternative is selected.
 for(auto& range:ranges)range={std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()};
 for(quint64 i=0;i<count;++i){const qsizetype entry=qsizetype(toc+4+i*entrySize);const auto off=number(source,entry+16,offsetBytes),n=number(source,entry+16+offsetBytes,4),type=number(source,entry+20+offsetBytes,4)>>24;
  check(off+n<=quint64(source.size()),"Source segment bounds");if(type>=7&&type<=16)shapeSegment(source.mid(qsizetype(off),qsizetype(n)),major,minor,width,damage,ranges,true);
 }
 for(quint64 i=0;i<count;++i){const qsizetype entry=qsizetype(toc+4+i*entrySize);const auto off=number(source,entry+16,offsetBytes),n=number(source,entry+16+offsetBytes,4),type=number(source,entry+20+offsetBytes,4)>>24;check(off+n<=quint64(source.size()),"Source segment bounds");if(type<7||type>16)continue;
  ++shapes;const auto segment=shapeSegment(source.mid(qsizetype(off),qsizetype(n)),major,minor,width,damage,ranges);
  check(quint64(output.size())+quint64(segment.size())<0xffffffffu,"Fixture output exceeds size limit");put(output,entry+16,output.size(),offsetBytes);put(output,entry+16+offsetBytes,segment.size(),4);output+=segment;
 }check(shapes>0,"No display shape");return output;
}
void save(const QString& path,const QByteArray& data){QFile file(path);check(file.open(QIODevice::WriteOnly)&&file.write(data)==data.size(),"Cannot write private fixture");}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
 check(argc==3,"Usage: jt_coordinate_fixturegen <local-single-part-9.x-or-10.0.jt> <output-folder>");const auto input=QString::fromLocal8Bit(argv[1]);QFile file(input);check(file.open(QIODevice::ReadOnly),"Cannot open local source");const auto source=file.readAll();
 unsigned major,minor;if(source.startsWith("Version 9.0 ")){major=9;minor=0;}else if(source.startsWith("Version 9.5 ")){major=9;minor=5;}else{check(source.startsWith("Version 10.0 "),"Unqualified fixture source revision");major=10;minor=0;}
 auto baseline=Jt::Importer::read(input);check(baseline.scene->mNumMeshes==1,"Fixture needs one source mesh");const auto& original=*baseline.scene->mMeshes[0];QDir folder(QString::fromLocal8Bit(argv[2]));check(QDir().mkpath(folder.path()),"Cannot create output directory");save(folder.filePath("lossless_reference.jt"),source);
 for(unsigned width:{8u,16u}){
  const auto path=folder.filePath(QString("quantized_%1.jt").arg(width));Ranges ranges;save(path,fixture(source,major,minor,width,0,ranges));auto result=Jt::Importer::read(path);check(result.scene->mNumMeshes==1,"Quantized control changed mesh count");const auto& mesh=*result.scene->mMeshes[0];
  check(mesh.mNumVertices==original.mNumVertices&&mesh.mNumFaces==original.mNumFaces,"Quantized control changed geometry counts");
  for(unsigned i=0;i<mesh.mNumVertices;++i){check(mesh.mNormals[i]==original.mNormals[i],"Quantized control changed stored normals");for(unsigned c=0;c<3;++c){const double lo=ranges[c][0],span=double(ranges[c][1])-lo,maximum=double((uint64_t(1)<<width)-1);const float expected=float(lo+quantize(original.mVertices[i][c],ranges[c],width)*span/maximum);check(mesh.mVertices[i][c]==expected,"Quantized control disagrees with independent rounded expectation");const double tolerance=span/(2*maximum)+std::max(1.0,std::abs(double(original.mVertices[i][c])))*2e-7;check(std::abs(double(mesh.mVertices[i][c])-original.mVertices[i][c])<=tolerance,"Coordinate error exceeds half-step plus float rounding");}}
  for(unsigned i=0;i<mesh.mNumFaces;++i)for(unsigned c=0;c<3;++c)check(mesh.mFaces[i].mIndices[c]==original.mFaces[i].mIndices[c],"Quantized control changed topology");
  auto copy=std::unique_ptr<aiScene>(SceneUtils::deepCopyScene(result.scene.get()));for(unsigned i=0;i<mesh.mNumVertices;++i)check(copy->mMeshes[0]->mVertices[i]==mesh.mVertices[i],"Scene copy changed coordinates");
  Assimp::Exporter exporter;const auto glb=path+".glb";check(exporter.Export(result.scene.get(),"glb2",glb.toStdString())==AI_SUCCESS,"Quantized GLB export failed");Assimp::Importer importer;const auto* loaded=importer.ReadFile(glb.toStdString(),0);check(loaded&&loaded->mNumMeshes==1&&loaded->mMeshes[0]->mNumFaces==mesh.mNumFaces,"Quantized GLB changed scene");
  for(unsigned i=0;i<mesh.mNumFaces;++i)for(unsigned c=0;c<3;++c)check(loaded->mMeshes[0]->mVertices[loaded->mMeshes[0]->mFaces[i].mIndices[c]]==mesh.mVertices[mesh.mFaces[i].mIndices[c]],"GLB changed rounded corners");
 }
 for(unsigned damage:{1u,2u}){Ranges ranges;const auto path=folder.filePath(damage==1?"invalid_width.jt":"invalid_code.jt");save(path,fixture(source,major,minor,16,damage,ranges));bool rejected=false;try{Jt::Importer::read(path);}catch(const std::exception& e){rejected=std::string(e.what()).find(damage==1?"uniform quantizer":"coordinate quantizer code")!=std::string::npos;}check(rejected,"Malformed coordinate control not rejected explicitly");}
 Fixture::writeSelection(folder.filePath("coordinate_lod_fallback.jt"),"invalid_code.jt","quantized_16.jt",true);const auto fallback=Jt::Importer::read(folder.filePath("coordinate_lod_fallback.jt"));check(fallback.scene->mNumMeshes==1&&!fallback.warnings.isEmpty(),"Coordinate failure lost usable LOD fallback");
 save(folder.filePath("README.txt"),"Private source-derived coordinate controls. Compare quantized_16 with lossless_reference; quantized_8 has larger deliberate position rounding without changing triangle count. Invalid width/code must fail and fallback must load. No downloaded geometry is redistributable by inference; hashes are placeholders.\n");
 std::cout<<"JT "<<major<<'.'<<minor<<" rounded 8/16-bit coordinates, half-step bounds, topology/normals, scene copy, GLB and fallback passed; vertices="<<original.mNumVertices<<" triangles="<<original.mNumFaces<<'\n';return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
