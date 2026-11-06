// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original literal in-memory tests of the PMI Manager Meta Data Element reader (JT 10.6 section 8.3, element version 5). The
// fixtures are written by this file from the published layout and from the field order observed in authentic Solid Edge
// JT 10.10 controls; they are not themselves authentic exporter evidence.
#include "JtContainerFixture.h"
#include <JtData_Model.hxx>
#include <JtData_Reader.hxx>
#include <JtElement_MetaData_PMIManager.hxx>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
using Fixture::Bytes;
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
struct PmiModel:JtData_Model {PmiModel(bool big):JtData_Model(TCollection_ExtendedString()){myMajorVersion=10;myMinorVersion=10;myIsFileLE=!big;}};
struct PmiReader:JtData_Reader {
 QByteArray data;size_t pos=0;
 explicit PmiReader(const Bytes& b):JtData_Reader(new PmiModel(b.big)),data(b.data){}
 Standard_Boolean ReadBytes(void* out,Standard_Size n)override{if(n>size_t(data.size())-pos)return false;if(n)std::memcpy(out,data.constData()+pos,n);pos+=n;return true;}
 Standard_Boolean SkipBytes(Standard_Size n)override{if(n>size_t(data.size())-pos)return false;pos+=n;return true;}
 Standard_Size GetPosition()const override{return pos;}
};
struct Spec {
 bool big=false;unsigned version=5,designGroups=0,associations=0,userAttributes=0,modelViews=0,textEntities=0;
 int badIndex=-1;bool oddVertexCoordinates=false;unsigned entityCountOverride=0;bool truncate=false;
};
// One dimension-like entity: label 3, a real 2D frame, two stored polylines (3 vertices), properties value and unit.
Bytes element(const Spec& s) {
 Bytes b(s.big);b.u8(s.version);b.u16(0xffff);b.u32(s.designGroups);b.u32(s.associations);b.u32(s.userAttributes);
 b.u32(2);b.text("Dimension");b.text("PMI");b.u32(s.modelViews);
 b.u32(s.entityCountOverride?s.entityCountOverride:1);
 b.u32(3);b.u8(1);for(float v:{0.1f,-0.05f,0.f,1.1f,-0.05f,0.f,0.1f,-0.05f,1.f})b.f32(v);b.f32(0.0065f);b.u8(1);b.u32(s.textEntities);
 b.u32(2);b.u32(0);b.u32(s.badIndex>=0?unsigned(s.badIndex):3);   // polyline indices (vertex indices)
 b.u32(2);b.u16(7);b.u16(0);                                       // polyline type values
 b.u32(0);                                                         // widths
 b.u32(s.oddVertexCoordinates?8:9);for(unsigned i=0;i<(s.oddVertexCoordinates?8u:9u);++i)b.f32(float(i));
 b.u32(2);b.text("value");b.u8(0);b.text("100");b.u8(0);b.text("unit");b.u8(0);b.text("millimeters");b.u8(0);
 b.u32(0);b.u32(1);b.u16(0x0082);b.u16(0x0001);b.u16(0);
 b.u32(0xdeadbeef);b.u32(0xdeadbeef);                              // trailing sections the reader leaves alone
 if(s.truncate)b.data.chop(40);
 return b;
}
template<class F>void rejects(F action,const char* why){try{action();}catch(const std::runtime_error&){return;}throw std::runtime_error(std::string("Expected rejection: ")+why);}
Handle(JtElement_MetaData_PMIManager) read(const Spec& s,size_t* consumed=nullptr,size_t* total=nullptr) {
 const auto bytes=element(s);PmiReader reader(bytes);Handle(JtElement_MetaData_PMIManager) pmi=new JtElement_MetaData_PMIManager;
 check(pmi->Read(reader),"PMI element read returned false");if(consumed)*consumed=reader.pos;if(total)*total=size_t(bytes.data.size());return pmi;
}
void verify(const Handle(JtElement_MetaData_PMIManager)& pmi) {
 check(pmi->LocalVersion()==5 && pmi->Strings().size()==2 && pmi->Strings()[0]==u"Dimension" && pmi->Strings()[1]==u"PMI","String table");
 check(pmi->Entities().size()==1,"One entity");
 const auto& e=pmi->Entities()[0];
 check(e.UserLabel==3 && e.HasFrame && e.Frame[0]==0.1f && e.Frame[3]==1.1f && e.Frame[8]==1.f,"Label and reference frame");
 check(e.TextHeight==0.0065f && e.SymbolValid==1,"Text height and validity");
 check(e.PolylineIndices==std::vector<int32_t>({0,3}) && e.PolylineTypes==std::vector<int16_t>({7,0}) && e.PolylineWidths.empty(),"Polyline indices and types");
 check(e.Vertices.size()==9 && e.Vertices[4]==4.f,"Vertices stay in stored order");
 check(e.Properties.size()==2 && e.Properties[0].Key==u"value" && e.Properties[0].Value==u"100" && e.Properties[1].Key==u"unit" && e.Properties[1].Value==u"millimeters","Ordered properties");
 check(e.TypeName==u"Dimension" && e.ParentTypeName==u"PMI" && e.EntityType==0x0082 && e.ParentType==0x0001 && e.UserFlags==0,"Type names and codes");
}
}
void pmiTests() {
 // Both byte orders read the same entity, and the reader stops before the trailing sections.
 for(bool big:{false,true}) {
  Spec s;s.big=big;size_t used=0,total=0;const auto pmi=read(s,&used,&total);verify(pmi);
  check(used+8==total,"The reader must leave exactly the trailing sections unread");
 }
 // Anything not evidenced is rejected, not guessed.
 { Spec s;s.version=4; rejects([&]{read(s);},"other element version"); }
 { Spec s;s.version=6; rejects([&]{read(s);},"later element version"); }
 { Spec s;s.designGroups=1; rejects([&]{read(s);},"design groups"); }
 { Spec s;s.associations=1; rejects([&]{read(s);},"associations"); }
 { Spec s;s.userAttributes=1; rejects([&]{read(s);},"user attributes"); }
 { Spec s;s.modelViews=1; rejects([&]{read(s);},"model views"); }
 { Spec s;s.textEntities=1; rejects([&]{read(s);},"2D text entities"); }
 // Malformed input.
 { Spec s;s.badIndex=4; rejects([&]{read(s);},"polyline index beyond the vertex array"); }
 { Spec s;s.oddVertexCoordinates=true; rejects([&]{read(s);},"coordinates that are not 3D triples"); }
 { Spec s;s.entityCountOverride=0x7fffffff; rejects([&]{read(s);},"implausible entity count"); }
 { Spec s;s.entityCountOverride=2; rejects([&]{read(s);},"declared entity missing"); }
 { Spec s;s.truncate=true; s.entityCountOverride=1; rejects([&]{read(s);},"truncated element"); }
}
