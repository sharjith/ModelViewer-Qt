// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original literal in-memory tests of the pure parts of recorded-PMI import (reading dimensions and annotation-like items out of
// a part's flattened PMI rows, and placing a point on a triangle mesh). They use hand-built rows and meshes; they are not
// evidence about any file.
#include "Jt/RecordedPmi.h"
#include "RecordedPmiSanity.h"
#include "RecordedDrawingJson.h"
#include <cmath>
#include <stdexcept>
#include <vector>
namespace {
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
bool near(double a,double b,double tolerance=1e-5){return std::abs(a-b)<=tolerance;}
void add(NodePropertySet& set,const QString& key,const QString& value,const QString& type=QStringLiteral("text")){set.rows.push_back({key,value,type});}
QString prefix(int n){return QStringLiteral("PMI %1").arg(n);}
void head(NodePropertySet& set,int n,const char* typeName,int code){
 add(set,prefix(n)+QLatin1Char(' ')+QLatin1String(typeName),QStringLiteral("label 1, 125 polyline vertices, 3 properties"),QStringLiteral("group"));
 if(code>=0)add(set,prefix(n)+QStringLiteral(".(entity type)"),QString::number(code),QStringLiteral("integer"));
}
void prop(NodePropertySet& set,int n,const char* key,const char* value){if(value)add(set,prefix(n)+QLatin1Char('.')+QLatin1String(key),QLatin1String(value));}
// A dimension entity; any argument left null is simply not written.
void dimension(NodePropertySet& set,int n,int code,const char* type,const char* style,const char* value,const char* from,const char* to,const char* frame=nullptr){
 head(set,n,"Dimension",code);prop(set,n,"type",type);prop(set,n,"style",style);prop(set,n,"value",value);prop(set,n,"unit","millimeters");prop(set,n,"precision","2");
 prop(set,n,"Leader[0].terminator",from);prop(set,n,"Leader[1].terminator",to);prop(set,n,"textOrigin","42 -50 -39");prop(set,n,"(frame origin)",frame);
}
}
void recordedPmiTests(){
 using namespace RecordedPmi;
 // Vector parsing is strict.
 QVector3D v;
 check(parseVector(QStringLiteral("1 -2 3.5"),v) && v==QVector3D(1,-2,3.5f),"Three numbers parse");
 check(parseVector(QStringLiteral("  1   2\t3 "),v) && v==QVector3D(1,2,3),"Extra whitespace is tolerated");
 { QVector3D c; // authored colours are 0x00bbggrr
   check(parseColour(QStringLiteral("0x00b43c78"),c) && near(c.x(),0x78/255.0) && near(c.y(),0x3c/255.0) && near(c.z(),0xb4/255.0),"Colour bytes are blue, green, red");
   check(parseColour(QStringLiteral("0x00dc6e00"),c) && near(c.x(),0) && near(c.z(),0xdc/255.0),"A second authored colour");
   check(!parseColour(QStringLiteral("0x00000000"),c) && !parseColour(QStringLiteral("0x00101010"),c),"A colour too dark to read is refused");
   check(!parseColour(QStringLiteral("blue"),c) && !parseColour(QString(),c),"Malformed colours are refused"); }
 check(!parseVector(QStringLiteral("1 2"),v) &&!parseVector(QStringLiteral("a b c"),v) && !parseVector(QStringLiteral("1 2 3 4"),v) && !parseVector(QStringLiteral("1 2 nan"),v) && !parseVector(QString(),v),"Malformed vectors are rejected");

 // Dimensions: the four kinds, in entity order, with what each needs; the rest are left out.
 NodePropertySet set;set.nodeName="part";
 add(set,QStringLiteral("JTRepresentation"),QStringLiteral("StoredTessellation"));
 dimension(set,0,0x82,"1",nullptr,"100","100 -50 0","0 -50 0","100 -50 0");              // linear
 dimension(set,1,0x82,"3","0","25","35 -50 21","","31 -50 33");                         // diameter: needs terminator and frame
 dimension(set,2,0x82,"3","1","12.5","19 -50 34",nullptr,"31 -50 33");                  // radius
 dimension(set,3,0x82,"2",nullptr,"2.356194490192345",nullptr,nullptr,"0 48 98");       // angle (radians)
 dimension(set,4,0x82,"3","5","7","19 -50 34",nullptr,"31 -50 33");                     // another radial style: left out
 dimension(set,5,0x82,"3","1","12.5","19 -50 34",nullptr,nullptr);                      // radius without a frame: left out
 dimension(set,6,0x82,"2",nullptr,"2.3",nullptr,nullptr,nullptr);                       // angle without a frame: left out
 dimension(set,7,0x82,"1",nullptr,"25","75 -50 25",nullptr);                            // linear with one terminator: left out
 dimension(set,8,0x82,"1",nullptr,nullptr,"75 -50 25","100 -50 25");                    // linear without a value: left out
 dimension(set,9,0x100,"1",nullptr,"25","75 -50 25","100 -50 25");                      // a note, not a dimension
 dimension(set,10,-1,"1",nullptr,"24.99999999999999","75 -50 25","100 -50 25");         // no type code: the type name decides
 const auto dims=dimensions(set);
 check(dims.size()==5,"Four dimension kinds plus the type-name fallback");
 check(dims[0].kind==DimensionKind::Linear && dims[0].entity==0 && near(dims[0].value,100) && dims[0].unit=="millimeters" && dims[0].precision==2 && dims[0].from==QVector3D(100,-50,0) && dims[0].to==QVector3D(0,-50,0) && dims[0].hasTextOrigin && dims[0].textOrigin==QVector3D(42,-50,-39),"Linear dimension fields");
 check(dims[1].kind==DimensionKind::Diameter && near(dims[1].value,25) && dims[1].from==QVector3D(35,-50,21) && dims[1].to==QVector3D(27,-50,45),"Diameter: the opposite rim point mirrors through the centre");
 check(dims[2].kind==DimensionKind::Radius && near(dims[2].value,12.5) && dims[2].from==QVector3D(19,-50,34) && dims[2].to==QVector3D(43,-50,32),"Radius is anchored on the same two rim points");
 check(dims[3].kind==DimensionKind::Angle && near(dims[3].value,2.356194490192345,1e-12) && dims[3].vertex==QVector3D(0,48,98),"Angle: the vertex is the frame origin");
 check(dims[4].kind==DimensionKind::Linear && dims[4].entity==10 && near(dims[4].value,25,1e-9),"Entity numbers above nine and the type-name fallback");
 NodePropertySet none;add(none,QStringLiteral("JTRepresentation"),QStringLiteral("Assembly"));
 check(dimensions(none).empty() && notes(none).empty(),"A node without PMI rows has nothing");

 // Annotation-like entities.
 NodePropertySet marks;
 head(marks,0,"Note",0x100);prop(marks,0,"Text[0].Item[0].string","Line one");prop(marks,0,"Text[0].Item[1].string","more");prop(marks,0,"Text[1].Item[0].string","Second");
 prop(marks,0,"Leader[0].terminator","9 -50 38");prop(marks,0,"textOrigin","-48 -50 97");
 head(marks,1,"Balloon",0x235);prop(marks,1,"Text[0].Item[0].string","Hole");prop(marks,1,"balloonId","Hole");prop(marks,1,"Leader[0].terminator","20 -24 27");
 head(marks,2,"Balloon",0x235);prop(marks,2,"balloonId","B7");prop(marks,2,"Leader[0].terminator","1 2 3");
 head(marks,3,"DatumTarget",0x88);prop(marks,3,"label","A");prop(marks,3,"index","2");prop(marks,3,"Leader[0].terminator","20 -24 27");
 head(marks,4,"Region",0x246);prop(marks,4,"Description","Datum Point 1");prop(marks,4,"(frame origin)","10 -50 60");
 head(marks,5,"SurfaceFinish",0x41);prop(marks,5,"Leader[0].terminator","10 24 100");prop(marks,5,"textOrigin","11 24 100");
 head(marks,6,"Region",0x246);prop(marks,6,"Description","nowhere");                       // nothing to place it by: left out
 head(marks,7,"Unknown",0x999);prop(marks,7,"Leader[0].terminator","1 1 1");               // not an annotation-like kind
 head(marks,8,"Note",0x100);prop(marks,8,"Text[0].Item[0].string","text origin only");prop(marks,8,"textOrigin","5 6 7");
 // Kinds generated with Solid Edge on 7 November 2026: datum feature symbol, feature control frame and line weld, placed by frame origin.
 head(marks,9,"DatumFeatureSymbol",0x84);prop(marks,9,"label","B");prop(marks,9,"(frame origin)","50 0 0");
 head(marks,10,"FeatureControlFrame",0x81);prop(marks,10,"(frame origin)","0 0 50");
 head(marks,11,"LineWeld",0x8);prop(marks,11,"(frame origin)","50 -48 0");
 const auto found=notes(marks);
 check(found.size()==10,"Ten placeable annotation-like entities");
 check(found[0].entityType==TypeCode::Note && found[0].text=="Line one more\nSecond" && found[0].hasLeader && found[0].anchor==QVector3D(9,-50,38) && found[0].hasTextOrigin && found[0].textOrigin==QVector3D(-48,-50,97),"Note text joins items with a space and texts with a newline");
 check(found[1].entityType==TypeCode::BalloonNote && found[1].text=="Hole" && found[2].text=="B7","Balloon text, falling back to its id");
 check(found[3].entityType==TypeCode::DatumTarget && found[3].text=="A2","Datum target label and index");
 check(found[4].entityType==TypeCode::Region && found[4].text=="Datum Point 1" && !found[4].hasLeader && found[4].hasAnchor && found[4].anchor==QVector3D(10,-50,60),"A region without a leader is placed by its frame origin");
 check(found[5].entityType==TypeCode::SurfaceFinish && found[5].text.isEmpty() && found[5].hasLeader,"A surface finish symbol carries no text");
 check(found[6].entity==8 && !found[6].hasLeader && found[6].anchor==QVector3D(5,6,7),"With nothing else, the text origin places an item");
 check(found[7].entityType==TypeCode::DatumFeatureSymbol && found[7].text=="B" && found[7].anchor==QVector3D(50,0,0),"A datum feature symbol carries its label and sits at its frame origin");
 check(found[8].entityType==TypeCode::FeatureControlFrame && found[8].text.isEmpty() && found[8].anchor==QVector3D(0,0,50),"A feature control frame is placed by its frame origin");
 check(found[9].entityType==TypeCode::LineWeld && found[9].anchor==QVector3D(50,-48,0),"A line weld is placed by its frame origin");

 // Closest point on a mesh: a unit square in z = 0 made of two triangles.
 const std::vector<float> points{0,0,0, 1,0,0, 1,1,0, 0,1,0};
 const std::vector<unsigned> indices{0,1,2, 0,2,3};
 auto hit=closestTriangle(points,indices,QVector3D(0.25f,0.75f,0.5f));
 check(hit.triangle==1 && near(hit.distance,0.5) && near(hit.barycentric.x(),0.25) && near(hit.barycentric.y(),0.25) && near(hit.barycentric.z(),0.5),"Interior point projects onto the second triangle");
 check(near(hit.barycentric.x()+hit.barycentric.y()+hit.barycentric.z(),1.0),"Barycentric weights sum to one");
 hit=closestTriangle(points,indices,QVector3D(1,0,0));
 check(hit.triangle==0 && near(hit.distance,0) && near(hit.barycentric.y(),1) && near(hit.barycentric.x(),0) && near(hit.barycentric.z(),0),"A vertex is found on the first triangle that holds it");
 hit=closestTriangle(points,indices,QVector3D(3,0,0));
 check(near(hit.distance,2) && near(hit.barycentric.y(),1),"A point beyond a vertex clamps to that vertex");
 hit=closestTriangle(points,indices,QVector3D(0.5f,-1,0));
 check(near(hit.distance,1) && near(hit.barycentric.x()+hit.barycentric.y(),1) && near(hit.barycentric.z(),0),"A point beyond an edge clamps to the edge");
 // A mesh with a degenerate triangle and a bad index still answers from the valid one.
 const std::vector<float> flat{0,0,0, 1,0,0, 2,0,0, 0,1,0};
 hit=closestTriangle(flat,std::vector<unsigned>{0,1,2, 0,1,9, 0,1,3},QVector3D(0.1f,0.1f,0));
 check(hit.triangle==2 && near(hit.distance,0),"Degenerate and out-of-range triangles are skipped");
 check(closestTriangle(points,std::vector<unsigned>{},QVector3D(0,0,0)).triangle==-1,"An empty mesh has no triangle");

 // Stored drawings: a frame in the z = 0 plane (so its normal is +z) and four polylines: a line, a closed triangle (an
 // arrowhead), and two text strokes (type 8).
 auto geometry=std::make_shared<PmiEntityGeometry>();
 geometry->hasFrame=true;
 const float frame[9]={0,0,0, 1,0,0, 0,1,0};
 for(int i=0;i<9;++i)geometry->frame[i]=frame[i];
 geometry->indices={0,2,6,8,12};
 geometry->types={2,0,4,8};
 geometry->vertices={0,0,0, 1,0,0,   0,0,0, 1,0,0, 1,1,0, 0,0,0,   2,0,0, 3,0,0,   2,1,0, 3,1,0, 3,2,0, 2,2,0};
 check(polylineTypes(*geometry)==std::vector<std::int16_t>({0,0,8,8}),"Pairs of (up to index, type) give each polyline its type");
 { PmiEntityGeometry none;none.indices={0,2,4};check(polylineTypes(none)==std::vector<std::int16_t>({0,0}),"Without type pairs every polyline is a general line");
   PmiEntityGeometry wide=none;wide.types={9,8};check(polylineTypes(wide)==std::vector<std::int16_t>({8,8}),"A pair reaching past the last polyline is clipped");
   PmiEntityGeometry broken=none;broken.types={2,8,1,0};check(polylineTypes(broken)==std::vector<std::int16_t>({8,0}) || polylineTypes(broken)==std::vector<std::int16_t>({8,8}),"A pair that goes backwards stops the reading");
   check(polylineTypes(PmiEntityGeometry()).empty(),"No indices, no polylines"); }
 check(flipAboutAnchor(QVector3D(3,1,0),QVector3D(2.5f,0,0),QVector3D(1,0,0))==QVector3D(2,1,0),"A point mirrors about the anchor along the frame's X axis");
 check(flipAboutAnchor(QVector3D(2.5f,7,3),QVector3D(2.5f,0,0),QVector3D(1,0,0))==QVector3D(2.5f,7,3),"A point on the mirror plane stays");

 QMatrix4x4 identity;
 check(!seenFromBehind(*geometry,identity,QVector3D(0,0,5)) && seenFromBehind(*geometry,identity,QVector3D(0,0,-5)),"The frame's normal decides which side is the front");
 QMatrix4x4 turned;turned.rotate(180,0,1,0);
 check(seenFromBehind(*geometry,turned,QVector3D(0,0,5)) && !seenFromBehind(*geometry,turned,QVector3D(0,0,-5)),"The render transform carries the plane with the mesh");
 { PmiEntityGeometry unframed=*geometry;unframed.hasFrame=false;check(!seenFromBehind(unframed,identity,QVector3D(0,0,-5)),"Without a frame nothing is behind"); }

 RecordedDrawing drawing;drawing.geometry=geometry;
 std::vector<float> lines,triangles;
 appendStrokes(drawing,identity,QVector3D(0,0,5),QVector3D(1,0.5f,0.25f),lines,&triangles);
 check(lines.size()==9*2*6,"Nine segments: 1 + 3 + 1, and the four-point text stroke closes into a four-edge letter loop");
 check(triangles.size()==18*6,"The closed triangle and the closed letter loop (two triangles) are filled, both faces");
 check(lines[0]==0 && lines[6]==1 && lines[3]==1 && lines[4]==0.5f && lines[5]==0.25f,"Vertices carry position and colour");
 { std::vector<float> outlineOnly;appendStrokes(drawing,identity,QVector3D(0,0,5),QVector3D(1,1,1),outlineOnly,nullptr);check(outlineOnly.size()==8*2*6,"Without a triangle buffer the stored polylines are drawn as they are");}
 // Seen from behind the text strokes mirror about the middle of their own extent, the rest does not move.
 const std::vector<std::int16_t> kinds=polylineTypes(*geometry);
 check(near(textPivot(*geometry,kinds,QVector3D(0,0,0),QVector3D(1,0,0)),2.5),"The pivot is the middle of the text strokes' extent along X");
 check(std::isnan(textPivot(*geometry,std::vector<std::int16_t>{0,0,0,0},QVector3D(0,0,0),QVector3D(1,0,0))),"Without text strokes there is no pivot");
 std::vector<float> front,back;
 appendStrokes(drawing,identity,QVector3D(0,0,5),QVector3D(1,1,1),front,nullptr);
 appendStrokes(drawing,identity,QVector3D(0,0,-5),QVector3D(1,1,1),back,nullptr);
 check(front[48]==2 && front[54]==3,"From the front the first text stroke keeps its place");
 check(back[48]==3 && back[54]==2,"From behind the first text stroke mirrors about the anchor");
 check(std::equal(front.begin(),front.begin()+48,back.begin()),"Geometry that is not text never mirrors");
 { RecordedDrawing unframed=drawing;auto copy=std::make_shared<PmiEntityGeometry>(*geometry);copy->hasFrame=false;unframed.geometry=copy;
   std::vector<float> flat;appendStrokes(unframed,identity,QVector3D(0,0,-5),QVector3D(1,1,1),flat,nullptr);check(flat==front,"Without a frame there is no back side and nothing mirrors");
   auto noText=std::make_shared<PmiEntityGeometry>(*geometry);noText->types={4,0};RecordedDrawing plain=drawing;plain.geometry=noText;
   std::vector<float> a,b;appendStrokes(plain,identity,QVector3D(0,0,5),QVector3D(1,1,1),a,nullptr);appendStrokes(plain,identity,QVector3D(0,0,-5),QVector3D(1,1,1),b,nullptr);check(a==b,"A drawing without text strokes looks the same from both sides"); }
 // A symbol's text group keeps its place: P and S swap sides about the group's centre, the symbol in the middle stays put.
 { auto symbol=std::make_shared<PmiEntityGeometry>();symbol->hasFrame=true;for(int i=0;i<9;++i)symbol->frame[i]=frame[i];
   symbol->indices={0,2,4,6};symbol->types={3,8};
   symbol->vertices={-6,0,0, -5,0,0,   -1,0,0, 1,0,0,   5,0,0, 6,0,0};   // "P", the symbol's middle text, "S"
   RecordedDrawing group;group.geometry=symbol;
   std::vector<float> behind;appendStrokes(group,identity,QVector3D(0,0,-5),QVector3D(1,1,1),behind,nullptr);
   check(behind[0]==6 && behind[6]==5 && behind[12]==1 && behind[18]==-1 && behind[24]==-5 && behind[30]==-6,"The group mirrors about its own centre: P and S swap sides, the middle stays in the middle"); }
 { RecordedDrawing broken=drawing;auto copy=std::make_shared<PmiEntityGeometry>(*geometry);copy->indices={0,2,99};broken.geometry=copy;
   std::vector<float> safe;appendStrokes(broken,identity,QVector3D(0,0,5),QVector3D(1,1,1),safe,nullptr);check(safe.size()==2*6,"A polyline past the vertex data is skipped, never read");
   RecordedDrawing empty;std::vector<float> none;appendStrokes(empty,identity,QVector3D(0,0,5),QVector3D(1,1,1),none,nullptr);check(none.empty(),"No geometry draws nothing"); }

 // MVF form: a round trip keeps everything, and malformed input is refused.
 drawing.meshUuid=QUuid::createUuid();
 const QJsonObject json=drawingToJson(drawing);
 const auto restored=drawingFromJson(json);
 check(restored && restored->meshUuid==drawing.meshUuid,"The mesh survives");
 check(restored->geometry && restored->geometry->hasFrame && restored->geometry->indices==geometry->indices && restored->geometry->types==geometry->types && restored->geometry->vertices==geometry->vertices,"Indices, types and vertices survive");
 check(std::equal(restored->geometry->frame,restored->geometry->frame+9,geometry->frame),"The frame survives");
 // Filled text: a square outline (two pieces) with a square hole (one closed piece) and an open stroke; the open stroke stays out.
 {
   PmiEntityGeometry glyph;glyph.hasFrame=true;for(int i=0;i<9;++i)glyph.frame[i]=frame[i];
   glyph.indices={0,3,6,11,13};
   glyph.types={4,8};
   glyph.vertices={0,0,0, 10,0,0, 10,10,0,   10,10,0, 0,10,0, 0,0,0,   3,3,0, 7,3,0, 7,7,0, 3,7,0, 3,3,0,   20,0,0, 21,0,0};
   const std::vector<std::int16_t> kinds={8,8,8,8};
   const std::vector<float> fill=glyphFillTriangles(glyph,kinds,8);
   double area=0;for(size_t i=0;i+8<fill.size();i+=9)area+=0.5*std::abs((fill[i+3]-fill[i])*(fill[i+7]-fill[i+1])-(fill[i+4]-fill[i+1])*(fill[i+6]-fill[i]));
   check(!fill.empty() && std::abs(area-84.0)<1e-3,"A ring of text outlines fills as outline minus hole");
   // A loop that only crosses another (the slash through the ring of the diameter sign) is a separate shape, not a hole.
   PmiEntityGeometry crossing;crossing.hasFrame=true;for(int i=0;i<9;++i)crossing.frame[i]=frame[i];
   crossing.indices={0,5,10};crossing.types={2,8};
   crossing.vertices={0,0,0, 4,0,0, 4,4,0, 0,4,0, 0,0,0,   3,3,0, 7,3,0, 7,7,0, 3,7,0, 3,3,0};
   const std::vector<float> crossFill=glyphFillTriangles(crossing,{8,8},8);
   double crossArea=0;for(size_t i=0;i+8<crossFill.size();i+=9)crossArea+=0.5*std::abs((crossFill[i+3]-crossFill[i])*(crossFill[i+7]-crossFill[i+1])-(crossFill[i+4]-crossFill[i+1])*(crossFill[i+6]-crossFill[i]));
   check(std::abs(crossArea-32.0)<1e-3,"Two overlapping squares are filled as two shapes");
   // A plain line whose two ends are the loose ends of a text outline completes the letter (the arc of the diameter sign).
   PmiEntityGeometry arc;arc.hasFrame=true;for(int i=0;i<9;++i)arc.frame[i]=frame[i];
   arc.indices={0,4,7};arc.types={1,8,2,0};
   arc.vertices={0,0,0, 4,0,0, 4,4,0, 0,4,0,   0,4,0, -1,2,0, 0,0,0};
   const GlyphFill closed=glyphFill(arc,polylineTypes(arc),8);
   double arcArea=0;for(size_t i=0;i+8<closed.triangles.size();i+=9)arcArea+=0.5*std::abs((closed.triangles[i+3]-closed.triangles[i])*(closed.triangles[i+7]-closed.triangles[i+1])-(closed.triangles[i+4]-closed.triangles[i+1])*(closed.triangles[i+6]-closed.triangles[i]));
   check(closed.absorbed==std::vector<size_t>{1},"The plain line that closes the outline is taken into the letter");
   check(std::abs(arcArea-18.0)<1e-3,"The letter is filled out to the absorbed arc, not cut off by a straight chord");
   // Smoothing leaves sharp corners where they are and rounds a coarse curve.
   std::vector<Glyph::P2> square={{0,0},{4,0},{4,4},{0,4}};
   const auto kept=Glyph::smoothLoop(square);bool cornersKept=true;for(const auto&c:square){bool found=false;for(const auto&p:kept)found=found||Glyph::same(p,c,1e-9);cornersKept=cornersKept&&found;}
   check(cornersKept,"A square keeps its corners");
   std::vector<Glyph::P2> octagon;for(int i=0;i<8;++i)octagon.push_back({std::cos(i*0.785398),std::sin(i*0.785398)});
   check(Glyph::smoothLoop(octagon).size()>octagon.size()*2,"An eight-sided curve is subdivided");
   PmiEntityGeometry unframed=glyph;unframed.hasFrame=false;check(glyphFillTriangles(unframed,kinds,8).empty(),"Without a frame nothing is filled");
   check(glyphFillTriangles(glyph,std::vector<std::int16_t>{0,0,0,0},8).empty(),"Only text strokes are filled");
   RecordedDrawing filled;filled.geometry=std::make_shared<PmiEntityGeometry>(glyph);
   std::vector<float> l,t;appendStrokes(filled,identity,QVector3D(0,0,5),QVector3D(1,1,1),l,&t);
   check(t.size()==fill.size()/9*2*3*6,"Both faces of every fill triangle are emitted");
   check(!l.empty(),"The filled text still has its outline");
 }
 { QJsonObject bad=json;bad.insert(QStringLiteral("indices"),packArray(std::vector<std::int32_t>{0,5,99}));check(!drawingFromJson(bad),"An index past the vertices is refused");
   bad=json;bad.insert(QStringLiteral("indices"),packArray(std::vector<std::int32_t>{0,6,2}));check(!drawingFromJson(bad),"Decreasing indices are refused");
   bad=json;bad.insert(QStringLiteral("vertices"),packArray(std::vector<float>{1,2}));check(!drawingFromJson(bad),"Vertices that are not triples are refused");
   bad=json;bad.insert(QStringLiteral("vertices"),QStringLiteral("AAA"));check(!drawingFromJson(bad),"A truncated array is refused");
   bad=json;bad.remove(QStringLiteral("vertices"));check(!drawingFromJson(bad),"A drawing without vertices is refused"); }
 { // Circles drawn as one line (circularity, concentricity) stay outlines; letter outlines are filled.
   using Glyph::P2;
   const auto polygon=[](double cu,double cv,double ru,double rv,int n){std::vector<P2> loop;for(int i=0;i<n;++i){const double a=6.283185307179586*i/n;loop.push_back({cu+ru*std::cos(a),cv+rv*std::sin(a)});}return loop;};
   const auto rectangle=[](double u0,double v0,double u1,double v1){return std::vector<P2>{{u0,v0},{u1,v0},{u1,v1},{u0,v1}};};
   std::vector<std::array<P2,3>> out;
   Glyph::fillLoops({polygon(0,0,1,1,24)},1e-6,out);
   check(out.empty(),"A single-line circle is not filled");
   out.clear();Glyph::fillLoops({polygon(0,0,1,1,24),polygon(0,0,0.6,0.6,24)},1e-6,out);
   check(out.empty(),"Two concentric single-line circles are not filled");
   out.clear();Glyph::fillLoops({polygon(0,0,0.8,1,24),polygon(0,0,0.4,0.6,24)},1e-6,out);
   check(!out.empty(),"An 'O' (oval outline with an oval hole) is filled");
   out.clear();Glyph::fillLoops({rectangle(0,0,0.25,1)},1e-6,out);
   check(!out.empty(),"A narrow stem ('I') is filled");
   out.clear();Glyph::fillLoops({polygon(0,0,1,1,24),polygon(4,0.9,0.05,0.05,12)},1e-6,out);
   check(!out.empty(),"A small dot next to a circle is still filled");
 }
 { // Import-time consistency check: quiet for snapping noise, loud only for a gross deviation.
   SanityReport close=checkAgainstMesh({1.0,1.02,0.97,1.04});
   check(close.compared==4&&close.off==0&&close.notes.isEmpty()&&close.warning.isEmpty(),"Values close to the mesh say nothing");
   SanityReport some=checkAgainstMesh({1.0,1.01,0.98,1.3});
   check(some.off==1&&some.notes.size()==1&&some.warning.isEmpty(),"A single outlier is logged, not shown");
   SanityReport units=checkAgainstMesh({25.4,25.5,25.3,25.4});
   check(units.off==4&&!units.warning.isEmpty(),"A systematic unit factor is shown to the user");
   check(near(units.medianRatio,25.4,0.1),"The median ratio is reported");
   SanityReport junk=checkAgainstMesh({0.0,-1.0,std::nan("")});
   check(junk.compared==0&&junk.warning.isEmpty()&&junk.notes.isEmpty(),"Unusable ratios are ignored");
   check(checkAgainstMesh({}).compared==0,"No dimensions, nothing to check");
 }
 { // Recorded angles against the extension lines drawn with them. The geometry is the 135 degree angle of the extended control (rounded):
   // vertex (0,48,98); one extension line straight up, one along (-1,0,-1); an arrowhead, an arc piece and a text stroke around them.
   PmiEntityGeometry g;
   g.hasFrame=true;
   const float lines[][3]={ {-6.402f,48,113.106f},{0,48,115.5f},{-6.832f,48,115.312f},{-6.402f,48,113.106f},   // arrowhead (4 vertices)
                            {0,48,101.371f},{0,48,118.87f},                                                      // extension line 1
                            {-2.383f,48,95.617f},{-14.758f,48,83.242f},                                          // extension line 2
                            {-23.486f,48,102.267f},{-24.086f,48,102.267f} };                                     // two text-stroke vertices
   for(const auto& p:lines){g.vertices.push_back(p[0]);g.vertices.push_back(p[1]);g.vertices.push_back(p[2]);}
   g.indices={0,4,6,8,10};
   g.types={3,0,4,8};   // polylines 0..2 plain, polyline 3 text
   double degrees=0;
   check(drawnAngleDegrees(g,QVector3D(0,48,98),degrees)&&near(degrees,135.0,0.05),"The angle between the two extension lines is read");
   check(!drawnAngleDegrees(g,QVector3D(30,48,98),degrees),"Lines that do not pass through the vertex are not extension lines");
   PmiEntityGeometry one=g;one.indices={0,4,6};one.types={2,0};
   check(!drawnAngleDegrees(one,QVector3D(0,48,98),degrees),"One extension line is not an angle");
   PmiEntityGeometry textOnly=g;textOnly.types={4,8};
   check(!drawnAngleDegrees(textOnly,QVector3D(0,48,98),degrees),"Text strokes are never extension lines");
   check(!drawnAngleDegrees(PmiEntityGeometry{},QVector3D(),degrees),"An empty drawing says nothing");
   SanityReport fine=checkAngles({1.0,1.01});
   check(fine.compared==2&&fine.off==0&&fine.notes.isEmpty()&&fine.warning.isEmpty(),"Angles that match their drawing say nothing");
   SanityReport radians=checkAngles({57.29578});
   check(radians.off==1&&!radians.warning.isEmpty(),"Radians read as degrees are shown to the user");
   check(checkAngles({}).compared==0,"No angles, nothing to check");
 }
}
