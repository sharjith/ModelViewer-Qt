// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original literal in-memory tests of the node-metadata flattening behind the read-only property table (C4a). They use
// hand-built Assimp metadata laid out like JtSceneBuilder::properties(); they say nothing about any authentic JT file.
#include "NodeProperties.h"
#include "Jt/JtLengthUnit.h"
#include <assimp/scene.h>
#include <memory>
#include <string>
#include <stdexcept>
namespace {
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
std::unique_ptr<aiMetadata> record(const char* key,const aiString* text,const int32_t* integer,const float* real){
 auto r=std::unique_ptr<aiMetadata>(aiMetadata::Alloc(3));
 r->Set(0,"Key",aiString(key));
 if(text)r->Set(1,"Value",*text);else if(integer)r->Set(1,"Value",*integer);else r->Set(1,"Value",*real);
 r->Set(2,"KeyAtomId",int32_t(7));
 return r;
}
}
void nodePropertiesTests(){
 // No metadata, or an empty block: nothing to show.
 aiNode bare;bare.mName=aiString("bare");
 check(!NodeProperties::fromNode(bare),"A node without metadata has no property set");
 aiNode empty;empty.mName=aiString("empty");empty.mMetaData=new aiMetadata;
 check(!NodeProperties::fromNode(empty),"A node with an empty metadata block has no property set");

 // Scalars of every kind keep their value text and a type label.
 aiNode scalars;scalars.mName=aiString("scalars");
 scalars.mMetaData=aiMetadata::Alloc(6);
 scalars.mMetaData->Set(0,"Flag",true);
 scalars.mMetaData->Set(1,"Count",int32_t(-4));
 scalars.mMetaData->Set(2,"Big",uint64_t(18446744073709551615ull));
 scalars.mMetaData->Set(3,"Ratio",0.5f);
 scalars.mMetaData->Set(4,"Name",aiString("Alpha"));
 scalars.mMetaData->Set(5,"Origin",aiVector3D(1.f,2.f,3.f));
 auto set=NodeProperties::fromNode(scalars);
 check(set && set->nodeName=="scalars" && set->rows.size()==6,"Scalar rows");
 check(set->rows[0].value=="true" && set->rows[0].type=="bool","Bool row");
 check(set->rows[1].value=="-4" && set->rows[1].type=="integer","Int32 row");
 check(set->rows[2].value=="18446744073709551615" && set->rows[2].type=="integer","Uint64 row is not truncated");
 check(set->rows[3].value=="0.5" && set->rows[3].type=="real","Float row");
 check(set->rows[4].key=="Name" && set->rows[4].value=="Alpha" && set->rows[4].type=="text","String row");
 check(set->rows[5].value=="1, 2, 3" && set->rows[5].type=="vector","Vector row");

 // JT layout: ordered Key/Value records become one row each, in order; other nested blocks become a count.
 const aiString part("Bracket"),unitName("millimeters");const int32_t quantity=12;const float mass=2.5f;
 std::unique_ptr<aiMetadata> list(aiMetadata::Alloc(4));
 list->Set(0,"0",*record("CAD_PART_NAME::",&part,nullptr,nullptr));
 list->Set(1,"1",*record("JT_PROP_MEASUREMENT_UNITS",&unitName,nullptr,nullptr));
 list->Set(2,"2",*record("Quantity",nullptr,&quantity,nullptr));
 list->Set(3,"3",*record("Mass",nullptr,nullptr,&mass));
 std::unique_ptr<aiMetadata> other(aiMetadata::Alloc(2));
 other->Set(0,"0",int32_t(1));other->Set(1,"1",int32_t(2));
 aiNode jt;jt.mName=aiString("part");
 jt.mMetaData=aiMetadata::Alloc(3);
 jt.mMetaData->Set(0,"JTRepresentation",aiString("StoredTessellation"));
 jt.mMetaData->Set(1,"JTProperties",*list);
 jt.mMetaData->Set(2,"JTTextureUnits",*other);
 set=NodeProperties::fromNode(jt);
 check(set && set->rows.size()==6,"JT rows: representation, four properties, one group");
 check(set->rows[0].key=="JTRepresentation","Top-level keys stay in order");
 check(set->rows[1].key=="CAD_PART_NAME::" && set->rows[1].value=="Bracket" && set->rows[1].type=="text","First JT property");
 check(set->rows[2].key=="JT_PROP_MEASUREMENT_UNITS" && set->rows[2].value=="millimeters","Unit property is shown as stored, not interpreted");
 check(set->rows[3].value=="12" && set->rows[3].type=="integer","Integer JT property");
 check(set->rows[4].value=="2.5" && set->rows[4].type=="real","Real JT property");
 check(set->rows[5].key=="JTTextureUnits" && set->rows[5].type=="group" && set->rows[5].value.contains("2"),"Other nested block is only counted");

 // A record without a usable Key/Value pair is skipped rather than shown half-filled.
 std::unique_ptr<aiMetadata> bad(aiMetadata::Alloc(2));
 std::unique_ptr<aiMetadata> keyOnly(aiMetadata::Alloc(1));keyOnly->Set(0,"Key",aiString("Orphan"));
 bad->Set(0,"0",*keyOnly);bad->Set(1,"1",*record("Kept",&part,nullptr,nullptr));
 aiNode skipped;skipped.mName=aiString("skipped");
 skipped.mMetaData=aiMetadata::Alloc(1);skipped.mMetaData->Set(0,"JTProperties",*bad);
 set=NodeProperties::fromNode(skipped);
 check(set && set->rows.size()==1 && set->rows[0].key=="Kept","Incomplete record skipped");
 // All records unusable: no set at all.
 std::unique_ptr<aiMetadata> onlyBad(aiMetadata::Alloc(1));onlyBad->Set(0,"0",*keyOnly);
 aiNode none;none.mName=aiString("none");none.mMetaData=aiMetadata::Alloc(1);none.mMetaData->Set(0,"JTProperties",*onlyBad);
 check(!NodeProperties::fromNode(none),"No usable rows gives no property set");

 // Row cap: a final truncation row, never more than the cap plus that row.
 const unsigned many=unsigned(NodeProperties::MaxRowsPerNode)+5;
 aiNode big;big.mName=aiString("big");big.mMetaData=aiMetadata::Alloc(many);
 for(unsigned i=0;i<many;++i)big.mMetaData->Set(i,std::to_string(i),int32_t(i));
 set=NodeProperties::fromNode(big);
 check(set && set->rows.size()==NodeProperties::MaxRowsPerNode+1 && set->rows.back().value=="truncated","Row cap adds one truncation row");
 // Declared length unit (C4b): reported from the JT property only when one recognised value is found.
 QStringList notes;
 check(Jt::declaredLengthUnit({},notes)==LengthUnit::Unknown && notes.isEmpty(),"No declared unit stays unknown without a note");
 check(Jt::declaredLengthUnit({QStringLiteral("millimeters")},notes)==LengthUnit::Millimeter,"Millimeters recognised");
 check(Jt::declaredLengthUnit({QStringLiteral("meters")},notes)==LengthUnit::Meter && notes.isEmpty(),"Meters recognised");
 check(Jt::declaredLengthUnit({QStringLiteral("Meters")},notes)==LengthUnit::Meter,"Spelling is matched without regard to case");
 check(Jt::declaredLengthUnit({QStringLiteral("centimeters")},notes)==LengthUnit::Centimeter && Jt::declaredLengthUnit({QStringLiteral("Inches")},notes)==LengthUnit::Inch && Jt::declaredLengthUnit({QStringLiteral("feet")},notes)==LengthUnit::Foot && notes.isEmpty(),"Centimeters, inches and feet are recognised");
 check(Jt::declaredLengthUnit({QStringLiteral("furlongs")},notes)==LengthUnit::Unknown && notes.size()==1 && notes[0].contains("furlongs"),"An unrecognised unit stays unknown and is explained");
 check(Jt::declaredLengthUnit({QStringLiteral("yards")},notes)==LengthUnit::Unknown && notes.size()==2,"A listed unit the viewer cannot represent stays unknown and is explained");
 notes.clear();
 check(Jt::declaredLengthUnit({QStringLiteral("meters"),QStringLiteral("millimeters")},notes)==LengthUnit::Unknown && notes.size()==1,"Disagreeing nodes leave the unit unknown and explain why");
}
