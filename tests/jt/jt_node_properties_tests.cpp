// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original literal in-memory tests of the node-metadata flattening behind the read-only property table (C4a). They use
// hand-built Assimp metadata laid out like JtSceneBuilder::properties(); they say nothing about any authentic JT file.
#include "NodeProperties.h"
#include "Jt/JtLengthUnit.h"
#include <assimp/scene.h>
#include <cmath>
#include <initializer_list>
#include <memory>
#include <utility>
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
 // Recorded PMI (C4c): one summary row per entity, then its properties prefixed with the entity number.
 {
  std::unique_ptr<aiMetadata> props(aiMetadata::Alloc(2));
  props->Set(0,"0",*record("value",&part,nullptr,nullptr));
  props->Set(1,"1",*record("unit",&unitName,nullptr,nullptr));
  std::unique_ptr<aiMetadata> entity(aiMetadata::Alloc(5));
  entity->Set(0,"TypeName",aiString("Dimension"));entity->Set(1,"UserLabel",int32_t(3));entity->Set(2,"PolylineVertices",int32_t(125));
  entity->Set(3,"PropertyCount",int32_t(2));entity->Set(4,"Properties",*props);
  std::unique_ptr<aiMetadata> pmi(aiMetadata::Alloc(1));pmi->Set(0,"0",*entity);
  aiNode node;node.mName=aiString("pmi");node.mMetaData=aiMetadata::Alloc(1);node.mMetaData->Set(0,"JTPMI",*pmi);
  const auto flat=NodeProperties::fromNode(node);
  check(flat && flat->rows.size()==3,"PMI rows: summary plus two properties");
  check(flat->rows[0].key=="PMI 0 Dimension" && flat->rows[0].type=="group" && flat->rows[0].value.contains("label 3") && flat->rows[0].value.contains("125"),"PMI summary row");
  check(flat->rows[1].key=="PMI 0.value" && flat->rows[1].value=="Bracket" && flat->rows[2].key=="PMI 0.unit" && flat->rows[2].value=="millimeters","PMI property rows keep order and entity prefix");
 }
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

 // Declared part mass: the JT "CAD_MASS::" property in kilograms, taken from the innermost property set that has a usable one.
 {
  const auto makeSet=[](const char* nodeName,std::initializer_list<std::pair<const char*,const char*>> rows){
   auto s=std::make_shared<NodePropertySet>();s->nodeName=QString::fromLatin1(nodeName);
   for(const auto& r:rows)s->rows.push_back({QString::fromLatin1(r.first),QString::fromLatin1(r.second),QStringLiteral("text")});
   return NodePropertySetPtr(s);
  };
  check(!NodeProperties::declaredMass(NodePropertyChain{}).valid(),"An empty chain declares no mass");
  check(!NodeProperties::declaredMass(NodePropertyChain{makeSet("a",{{"Mass","2.5"},{"JT_PROP_NAME","x"}})}).valid(),"Only the CAD_MASS:: key is a declared mass");
  const auto one=NodeProperties::declaredMass(NodePropertyChain{makeSet("part",{{"CAD_MASS::","3.460496e-03"}})});
  check(one.valid() && std::abs(one.kilograms-0.003460496)<1e-12 && one.sourceKey=="CAD_MASS::" && one.sourceNode=="part","A declared mass keeps its value, key and node");
  check(NodeProperties::declaredMass(NodePropertyChain{makeSet("p",{{"CAD_MASS::","6.063884944029302e-05"}})}).valid(),"Exponent notation is read");
  check(!NodeProperties::declaredMass(NodePropertyChain{makeSet("p",{{"CAD_MASS::","0"}})}).valid(),"A mass of zero is not a usable mass (the CAD has none to give)");
  check(NodeProperties::declaredMass(NodePropertyChain{makeSet("p",{{"CAD_MASS::","2"},{"CAD_MASS_UNITS::","kilograms"}})}).valid(),"The unit key, when present, may say kilograms");
  check(NodeProperties::declaredMass(NodePropertyChain{makeSet("p",{{"CAD_MASS::","2"},{"CAD_MASS_UNITS::"," Kilograms "}})}).valid(),"The unit is read without regard to case and spaces");
  check(!NodeProperties::declaredMass(NodePropertyChain{makeSet("p",{{"CAD_MASS::","2"},{"CAD_MASS_UNITS::","pounds"}})}).valid(),"Any other unit is not taken, nothing is converted by guess");
  check(!NodeProperties::declaredMass(NodePropertyChain{makeSet("p",{{"CAD_MASS::","2"},{"CAD_MASS_UNITS::",""}})}).valid(),"An empty unit entry is an unknown unit");
  {
   // an assembly's mass is not a part's: a set flagged massAggregate is skipped, so a part without a mass of its own gets none
   auto leaf=std::make_shared<NodePropertySet>();leaf->nodeName="leaf";leaf->rows.push_back({QStringLiteral("JT_PROP_NAME"),QStringLiteral("x"),QStringLiteral("text")});
   auto assembly=std::make_shared<NodePropertySet>();assembly->nodeName="assembly";assembly->rows.push_back({QStringLiteral("CAD_MASS::"),QStringLiteral("1305053.8"),QStringLiteral("text")});
   check(NodeProperties::declaredMass(NodePropertyChain{leaf,assembly}).valid(),"Without the aggregate flag the ancestor's mass would be taken");
   assembly->massAggregate=true;
   check(!NodeProperties::declaredMass(NodePropertyChain{leaf,assembly}).valid(),"An assembly's mass is never taken by a part below it");
   auto zeroLeaf=std::make_shared<NodePropertySet>();zeroLeaf->nodeName="zeroLeaf";zeroLeaf->rows.push_back({QStringLiteral("CAD_MASS::"),QStringLiteral("0"),QStringLiteral("text")});
   check(!NodeProperties::declaredMass(NodePropertyChain{zeroLeaf,assembly}).valid(),"A part with a zero mass does not fall back to its assembly's mass");
   check(NodeProperties::declaresMassKey(*zeroLeaf) && !NodeProperties::declaresMassKey(*leaf),"A zero still counts as a mass entry when looking for assemblies");
   // the flag travels through fromNode
   aiNode flagged;flagged.mName=aiString("flagged");flagged.mMetaData=aiMetadata::Alloc(1);flagged.mMetaData->Set(0,"Key",aiString("v"));
   check(!NodeProperties::fromNode(flagged)->massAggregate && NodeProperties::fromNode(flagged,nullptr,true)->massAggregate,"fromNode records the aggregate flag");
   aiNode massNode;massNode.mName=aiString("massNode");massNode.mMetaData=aiMetadata::Alloc(1);
   std::unique_ptr<aiMetadata> massList(aiMetadata::Alloc(1));
   const aiString massText("2.5");massList->Set(0,"0",*record("CAD_MASS::",&massText,nullptr,nullptr));
   massNode.mMetaData->Set(0,"JTProperties",*massList);
   check(NodeProperties::declaresMassKey(massNode) && !NodeProperties::declaresMassKey(flagged),"declaresMassKey reads the node's JT properties");
  }
  const auto inner=NodeProperties::declaredMass(NodePropertyChain{makeSet("inner",{{"CAD_MASS::","1.5"}}),makeSet("outer",{{"CAD_MASS::","9"}})});
  check(inner.valid() && inner.kilograms==1.5 && inner.sourceNode=="inner","The innermost node's mass wins over an ancestor's");
  const auto outer=NodeProperties::declaredMass(NodePropertyChain{makeSet("inner",{{"JT_PROP_NAME","x"}}),nullptr,makeSet("outer",{{"CAD_MASS::","9"}})});
  check(outer.valid() && outer.kilograms==9.0 && outer.sourceNode=="outer","A mass is found on an ancestor when the inner nodes have none; null sets are skipped");
  for(const char* unusable:{"","abc","-1","nan","inf","1,5","2 kg"})
   check(!NodeProperties::declaredMass(NodePropertyChain{makeSet("p",{{"CAD_MASS::",unusable}})}).valid(),"A value that is not a finite number >= 0 is not a mass");
  const auto fallThrough=NodeProperties::declaredMass(NodePropertyChain{makeSet("inner",{{"CAD_MASS::","abc"}}),makeSet("outer",{{"CAD_MASS::","4"}})});
  check(fallThrough.valid() && fallThrough.sourceNode=="outer","An unusable value on the inner node does not hide a usable one further out");
 }

 // Declared material: the STEP importer's "STEP_MATERIAL_NAME", taken as written from the innermost property set that has a non-empty one.
 {
  const auto makeSet=[](const char* nodeName,std::initializer_list<std::pair<const char*,const char*>> rows){
   auto s=std::make_shared<NodePropertySet>();s->nodeName=QString::fromLatin1(nodeName);
   for(const auto& r:rows)s->rows.push_back({QString::fromLatin1(r.first),QString::fromLatin1(r.second),QStringLiteral("text")});
   return NodePropertySetPtr(s);
  };
  check(!NodeProperties::declaredMaterial(NodePropertyChain{}).valid(),"An empty chain declares no material");
  check(!NodeProperties::declaredMaterial(NodePropertyChain{makeSet("a",{{"Material","Steel"},{"CAD_MASS::","1"}})}).valid(),"Only the STEP_MATERIAL_NAME key is a declared material");
  const auto one=NodeProperties::declaredMaterial(NodePropertyChain{makeSet("part",{{"STEP_MATERIAL_NAME","  Steel, Mild "}})});
  check(one.valid() && one.name=="Steel, Mild" && one.sourceKey=="STEP_MATERIAL_NAME" && one.sourceNode=="part","A declared material keeps its text (trimmed), key and node; no catalogue matching");
  check(NodeProperties::declaredMaterial(NodePropertyChain{makeSet("p",{{"STEP_MATERIAL_NAME","Generic"}})}).name=="Generic","A placeholder designation such as Generic is reported as written");
  check(!NodeProperties::declaredMaterial(NodePropertyChain{makeSet("p",{{"STEP_MATERIAL_NAME","   "}})}).valid(),"A blank name is no material");
  const auto inner=NodeProperties::declaredMaterial(NodePropertyChain{makeSet("inner",{{"STEP_MATERIAL_NAME","Brass"}}),makeSet("outer",{{"STEP_MATERIAL_NAME","Steel"}})});
  check(inner.valid() && inner.name=="Brass" && inner.sourceNode=="inner","The innermost node's material wins over an ancestor's");
  const auto outer=NodeProperties::declaredMaterial(NodePropertyChain{makeSet("inner",{{"JT_PROP_NAME","x"}}),nullptr,makeSet("outer",{{"STEP_MATERIAL_NAME","Steel"}})});
  check(outer.valid() && outer.name=="Steel" && outer.sourceNode=="outer","A material is found on an ancestor when the inner nodes have none; null sets are skipped");
 }
}
