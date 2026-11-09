// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original private UV fixture modifier, derived from our colour fixture tool.
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
namespace {
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
struct ColourModel:JtData_Model {ColourModel(unsigned major,unsigned minor):JtData_Model(TCollection_ExtendedString()){myMajorVersion=major;myMinorVersion=minor;myIsFileLE=true;}};
struct Reader:JtData_Reader {
 QByteArray bytes;size_t pos=0;Reader(QByteArray b,unsigned major,unsigned minor):JtData_Reader(new ColourModel(major,minor)),bytes(std::move(b)){}
 Standard_Boolean ReadBytes(void* p,Standard_Size n)override{if(n>size_t(bytes.size())-pos)return false;if(n)std::memcpy(p,bytes.constData()+pos,n);pos+=n;return true;}
 Standard_Boolean SkipBytes(Standard_Size n)override{if(n>size_t(bytes.size())-pos)return false;pos+=n;return true;}
 Standard_Size GetPosition()const override{return pos;}
};
quint64 number(const QByteArray& b,qsizetype at,unsigned size){check(at>=0&&at+size<=b.size(),"Fixture read bounds");quint64 v=0;for(unsigned i=0;i<size;++i)v|=quint64(quint8(b[at+i]))<<(8*i);return v;}
void put(QByteArray& b,qsizetype at,quint64 value,unsigned size){check(at>=0&&at+size<=b.size(),"Fixture write bounds");for(unsigned i=0;i<size;++i)b[at+i]=char(value>>(8*i));}
quint64 uvBinding(unsigned sets,const std::string& dimensions){quint64 value=0;for(unsigned i=0;i<sets;++i)value|=quint64(dimensions[i]=='0'?0:dimensions[i]=='1'?0x100:0x200)<<(4*i);return value;}
QByteArray graphSegment(const QByteArray& source,unsigned major,bool coloured,unsigned sets,const std::string& dimensions){
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
   if(major>=10)bindings={end-8};
   else if(number(data,end-22,2)==2)bindings={end-20,end-8};
   else {check(number(data,end-14,2)==1,"Unqualified source vertex-node local version");bindings={end-12};}
   for(auto at:bindings)put(data,at,(number(data,at,8)&~quint64(0xffffffff30ULL))|(uvBinding(sets,dimensions)|quint64(coloured?0x10:0)),8);++shapes;
  }pos+=4+qsizetype(n);
 }
 check(shapes>0,"No source triangle-strip node");Fixture::Bytes envelope;envelope.u32(0);envelope.u32(unsigned(data.size()+1));envelope.u8(0);envelope.data+=data;
 auto result=source.left(24)+envelope.data;put(result,20,result.size(),4);return result;
}
void packet(Fixture::Bytes& b,const std::vector<uint32_t>& v,bool lag){b.u32(unsigned(v.size()));if(v.empty())return;b.u8(0);b.u32(unsigned(v.size()*32));for(size_t i=0;i<v.size();++i)b.u32(lag&&i>=4?v[i]-v[i-1]:v[i]);}
QByteArray shapeSegment(const QByteArray& source,unsigned major,unsigned minor,unsigned mode,unsigned sets,const std::string& dimensions){
 auto data=source.mid(24);check(number(data,4,4)==0x10dd10ab,"Fixture needs TriStrip LOD");Reader reader(data,major,minor);check(reader.SkipBytes(25),"Short shape header");Jt_I16 v;Jt_U64 binding;
 check(reader.ReadVersion(v)&&reader.ReadVersion(v),"Short shape versions");const auto outer=reader.pos;check(reader.ReadU64(binding)&&(binding&0xffffffff30ULL)==0,"Source already has colours/UVs");
 size_t innerLength=0;if(major>=10){innerLength=reader.pos;check(reader.SkipBytes(25),"Short inner shape envelope");}
 Jt_I32 id;check(reader.ReadVersion(v)&&reader.ReadI32(id)&&reader.ReadVersion(v),"Short mesh versions");
 auto topology=JtDecode_MeshCoderDriver::LoadInputData(reader);check(topology.operator bool(),"Cannot scan source topology");
 const auto inner=reader.pos;check(reader.ReadU64(binding)&&(binding&0xffffffff30ULL)==0,"Source inner binding already has colours/UVs");check(reader.SkipBytes(4),"Short quantization parameters");Jt_I32 vertices,attributes;
 check(reader.ReadI32(vertices)&&reader.ReadI32(attributes)&&vertices>0&&attributes>0&&attributes<1000000,"Invalid fixture vertex count");
 auto coords=JtDecode_VertexData::LoadCompressedCoords(reader),normals=JtDecode_VertexData::LoadCompressedNormals(reader);
 check(coords.operator bool()&&normals.operator bool()&&normals->GetOutVertexCount()==attributes,"Cannot scan source vertex arrays");
 // Binary record-order patterns test transport and channel identity, not UV
 // unwrapping. Many triangles intentionally share UV points/lines; they are
 // unsuitable as a full-image surface-coverage oracle.
 const bool quantized=mode!=0;Fixture::Bytes uv;
 for(unsigned channel=0;channel<sets;++channel){const unsigned components=unsigned(dimensions[channel]-'0');if(!components)continue;uv.u32(attributes);uv.u8(components);uv.u8(mode==2?25:quantized?3:0);
 if(quantized)for(unsigned c=0;c<components;++c){uv.f32(0);uv.f32(1);uv.u8(1);}
 for(unsigned c=0;c<components;++c){std::vector<uint32_t> values,mantissae;
  for(int i=0;i<attributes;++i){const float value=float((unsigned(i)/((c?10u:5u)*(1u<<channel)))%2);uint32_t word;std::memcpy(&word,&value,4);
   values.push_back(quantized?uint32_t(value):major==9?word>>23:word);mantissae.push_back(word&0x7fffff);}
  packet(uv,values,quantized||major>=10);if(!quantized&&major==9)packet(uv,mantissae,false);
 }
 uv.u32(0);}
 if(mode==3){Fixture::Bytes colours;colours.u32(attributes);colours.u8(3);colours.u8(0);
  for(unsigned c=0;c<3;++c){const uint32_t word=c==1?0:0x3f800000;packet(colours,std::vector<uint32_t>(size_t(attributes),major==9?word>>23:word),major>=10);if(major==9)packet(colours,std::vector<uint32_t>(size_t(attributes),word&0x7fffff),false);}
  colours.u32(0);uv.data.prepend(colours.data);
 }
 data.insert(qsizetype(reader.pos),uv.data);
 for(auto at:{outer,inner})put(data,qsizetype(at),(number(data,qsizetype(at),8)&~quint64(0xffffffff30ULL))|(uvBinding(sets,dimensions)|quint64(mode==3?0x10:0)),8);
 put(data,0,number(data,0,4)+quint64(uv.data.size()),4);
 if(major>=10)put(data,qsizetype(innerLength),number(data,qsizetype(innerLength),4)+quint64(uv.data.size()),4);
 auto result=source.left(24)+data;put(result,20,result.size(),4);return result;
}
QByteArray fixture(const QByteArray& source,unsigned major,unsigned minor,unsigned mode,unsigned sets,const std::string& dimensions){
 check(number(source,80,1)==0,"Fixture needs little-endian input");auto output=source;const unsigned offsetBytes=major>=10?8:4,entrySize=major>=10?32:28;
 const auto toc=number(source,85,offsetBytes),count=number(source,qsizetype(toc),4);check(count>0&&count<10000,"Bad source TOC");unsigned shapes=0;
 for(quint64 i=0;i<count;++i){const qsizetype entry=qsizetype(toc+4+i*entrySize);const auto off=number(source,entry+16,offsetBytes),n=number(source,entry+16+offsetBytes,4),type=number(source,entry+20+offsetBytes,4)>>24;check(off+n<=quint64(source.size()),"Source segment bounds");QByteArray segment;
  if(type==1)segment=graphSegment(source.mid(qsizetype(off),qsizetype(n)),major,mode==3,sets,dimensions);
  else if(type>=7&&type<=16){segment=shapeSegment(source.mid(qsizetype(off),qsizetype(n)),major,minor,mode,sets,dimensions);++shapes;}else continue;
  check(quint64(output.size())+quint64(segment.size())<0xffffffffu,"Fixture output exceeds supported size");put(output,entry+16,output.size(),offsetBytes);put(output,entry+16+offsetBytes,segment.size(),4);output+=segment;
 }check(shapes>0,"No display shape");return output;
}
void save(const QString& path,const QByteArray& data){QFile f(path);check(f.open(QIODevice::WriteOnly)&&f.write(data)==data.size(),"Cannot write private fixture");}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
 check(argc>=3&&argc<=5,"Usage: jt_uv_fixturegen <local-single-part-9.x-or-10.0.jt> <output-folder> [uv-set-count 1-4] [component-pattern e.g. 0201, 0 means absent]");const unsigned sets=argc>=4?unsigned(std::stoul(argv[3])):1;check(sets>=1&&sets<=4,"Unqualified fixture UV set count");const std::string dimensions=argc==5?argv[4]:std::string(sets,'2');check(dimensions.size()==sets&&dimensions.find_first_not_of("012")==std::string::npos&&dimensions.back()!='0',"Invalid fixture component pattern");const auto input=QString::fromLocal8Bit(argv[1]);QFile f(input);check(f.open(QIODevice::ReadOnly),"Cannot open local source");const auto source=f.readAll();
 unsigned major,minor;if(source.startsWith("Version 9.0 ")){major=9;minor=0;}else if(source.startsWith("Version 9.5 ")){major=9;minor=5;}else {check(source.startsWith("Version 10.0 "),"Unqualified fixture source version");major=10;minor=0;}
 auto baseline=Jt::Importer::read(input);check(baseline.scene->mNumMeshes==1,"Fixture needs one source mesh");const auto& original=*baseline.scene->mMeshes[0];QDir folder(QString::fromLocal8Bit(argv[2]));check(QDir().mkpath(folder.path()),"Cannot create fixture directory");
 for(unsigned mode=0;mode<4;++mode){const auto path=folder.filePath(mode==0?"lossless_uv.jt":mode==1?"quantized_uv.jt":mode==2?"invalid_uv.jt":"colour_and_uv.jt");save(path,fixture(source,major,minor,mode,sets,dimensions));
  if(mode==2){bool rejected=false;try{Jt::Importer::read(path);}catch(const std::exception& e){rejected=std::string(e.what()).find("texture-coordinate quantization width")!=std::string::npos;}check(rejected,"Invalid UV quantization not rejected");continue;}
  auto result=Jt::Importer::read(path);check(result.scene->mNumMeshes==1,"Fixture changed mesh count");const auto& mesh=*result.scene->mMeshes[0];
  check(mesh.mNumVertices==original.mNumVertices&&mesh.mNumFaces==original.mNumFaces&&mesh.HasTextureCoords(0),"Fixture geometry count/UV binding changed");
  for(unsigned i=0;i<mesh.mNumVertices;++i)check(mesh.mVertices[i]==original.mVertices[i]&&mesh.mNormals[i]==original.mNormals[i],"Fixture changed coordinates/normals");
  for(unsigned i=0;i<mesh.mNumFaces;++i)for(unsigned c=0;c<3;++c)check(mesh.mFaces[i].mIndices[c]==original.mFaces[i].mIndices[c],"Fixture changed triangle indices");
  auto copy=std::unique_ptr<aiScene>(SceneUtils::deepCopyScene(result.scene.get()));check(copy->mMeshes[0]->HasTextureCoords(0),"Scene copy lost UVs");
  Assimp::Exporter exporter;const auto glb=path+".glb";check(exporter.Export(result.scene.get(),"glb2",glb.toStdString())==AI_SUCCESS,"UV GLB export failed");Assimp::Importer importer;const auto* loaded=importer.ReadFile(glb.toStdString(),0);check(loaded&&loaded->mNumMeshes==1&&loaded->mMeshes[0]->HasTextureCoords(0),"UV GLB reimport failed");
  // Assimp may discard an unreferenced source vertex on GLB export. Verify
  // rendered corners rather than assuming the exported vertex numbering.
  const auto& roundtrip=*loaded->mMeshes[0];check(roundtrip.mNumFaces==mesh.mNumFaces,"GLB changed triangle count");
  for(unsigned channel=0;channel<sets;++channel){check(mesh.mNumUVComponents[channel]==2&&copy->mMeshes[0]->mNumUVComponents[channel]==mesh.mNumUVComponents[channel]&&copy->mMeshes[0]->HasTextureCoords(channel)&&roundtrip.HasTextureCoords(channel),"Copy/GLB lost UV channel");for(unsigned i=0;i<mesh.mNumVertices;++i){check(copy->mMeshes[0]->mTextureCoords[channel][i]==mesh.mTextureCoords[channel][i],"Scene copy changed UVs");if(dimensions[channel]=='0')check(mesh.mTextureCoords[channel][i]==aiVector3D(),"Absent source UV slot did not pad to zero");if(dimensions[channel]=='1')check(mesh.mTextureCoords[channel][i].y==0,"Scalar texture coordinate did not embed V=0");}}
  for(unsigned i=0;i<mesh.mNumFaces;++i)for(unsigned c=0;c<3;++c){const unsigned a=mesh.mFaces[i].mIndices[c],b=roundtrip.mFaces[i].mIndices[c];
   for(unsigned channel=0;channel<sets;++channel)check(roundtrip.mVertices[b]==mesh.mVertices[a]&&roundtrip.mTextureCoords[channel][b]==mesh.mTextureCoords[channel][a],"GLB changed rendered UV corners");
  }
  if(mode==3){check(mesh.HasVertexColors(0),"Combined colour/UV binding lost colours");for(unsigned i=0;i<mesh.mNumVertices;++i)check(mesh.mColors[0][i]==aiColor4D(1,0,1,1),"Combined colour/UV data changed");}
  if(mode){auto reference=Jt::Importer::read(folder.filePath("lossless_uv.jt"));for(unsigned channel=0;channel<sets;++channel)for(unsigned i=0;i<mesh.mNumVertices;++i)check(mesh.mTextureCoords[channel][i]==reference.scene->mMeshes[0]->mTextureCoords[channel][i],"UV transports disagree");}
 }
 save(folder.filePath("unsupported_uv_sets.jt"),fixture(source,major,minor,0,5,std::string(5,'2')));
 {bool rejected=false;try{Jt::Importer::read(folder.filePath("unsupported_uv_sets.jt"));}catch(const std::exception& e){rejected=std::string(e.what()).find("Unsupported JT texture-coordinate layout")!=std::string::npos;}check(rejected,"Unsupported fifth UV set not rejected");}
 Fixture::writeSelection(folder.filePath("uv_layout_fallback.jt"),"unsupported_uv_sets.jt","lossless_uv.jt",true);
 {auto recovered=Jt::Importer::read(folder.filePath("uv_layout_fallback.jt"));check(recovered.scene->mNumMeshes==1&&!recovered.warnings.isEmpty(),"Unsupported UV layout lost LOD fallback");for(unsigned channel=0;channel<sets;++channel)check(recovered.scene->mMeshes[0]->HasTextureCoords(channel),"UV layout fallback lost a supported set");}
 Fixture::writeSelection(folder.filePath("uv_lod_fallback.jt"),"invalid_uv.jt","lossless_uv.jt",true);auto fallback=Jt::Importer::read(folder.filePath("uv_lod_fallback.jt"));check(fallback.scene->mNumMeshes==1&&fallback.scene->mMeshes[0]->HasTextureCoords(0)&&!fallback.warnings.isEmpty(),"UV failure lost usable LOD fallback");
 Fixture::writeDrawStyle(folder.filePath("uv_flat.jt"),"lossless_uv.jt",0x1a);auto flat=Jt::Importer::read(folder.filePath("uv_flat.jt"));check(flat.scene->mNumMeshes==1&&flat.scene->mMeshes[0]->HasTextureCoords(0)&&flat.scene->mMeshes[0]->mNumVertices==3*original.mNumFaces,"Flat expansion lost UVs");
 auto reference=Jt::Importer::read(folder.filePath("lossless_uv.jt"));const auto& smooth=*reference.scene->mMeshes[0];const auto& flatMesh=*flat.scene->mMeshes[0];
 for(unsigned channel=0;channel<sets;++channel){check(flatMesh.HasTextureCoords(channel),"Flat expansion lost UV channel");for(unsigned i=0;i<smooth.mNumFaces;++i)for(unsigned j=0;j<3;++j)check(flatMesh.mTextureCoords[channel][3*i+j]==smooth.mTextureCoords[channel][smooth.mFaces[i].mIndices[j]],"Flat UV corner mapping changed");}
 save(folder.filePath("README.txt"),"Private source-derived UV controls. No source textures are imported. Lossless and quantized files retain matching UV sets. Use the optional set-count argument to select a span of 1-4 slots; a zero component-pattern digit means absent source data and zero-padded scene UVs. Invalid UV and unsupported five-set layouts must fail; fallback controls must load. Hash placeholders are not independent conformance evidence. Geometry is not redistributable by inference.\n");
 std::cout<<"JT "<<major<<'.'<<minor<<" UV controls ("<<sets<<" sets, components "<<dimensions<<"), source geometry, scene copy, GLB and fallback passed; vertices="<<original.mNumVertices<<" triangles="<<original.mNumFaces<<'\n';return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
