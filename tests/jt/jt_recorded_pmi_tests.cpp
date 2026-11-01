// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original literal in-memory tests of the pure parts of recorded-PMI import (reading linear dimensions out of a part's flattened
// PMI rows, and placing a point on a triangle mesh). They use hand-built rows and meshes; they are not evidence about any file.
#include "RecordedPmi.h"
#include <cmath>
#include <stdexcept>
#include <vector>
namespace {
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
bool near(double a,double b,double tolerance=1e-5){return std::abs(a-b)<=tolerance;}
void add(NodePropertySet& set,const QString& key,const QString& value,const QString& type=QStringLiteral("text")){set.rows.push_back({key,value,type});}
void dimension(NodePropertySet& set,int n,const char* typeName,const char* type,const char* value,const char* from,const char* to){
 const QString p=QStringLiteral("PMI %1").arg(n);
 add(set,p+QLatin1Char(' ')+QLatin1String(typeName),QStringLiteral("label 1, 125 polyline vertices, 3 properties"),QStringLiteral("group"));
 if(type)add(set,p+QStringLiteral(".type"),QLatin1String(type));
 if(value)add(set,p+QStringLiteral(".value"),QLatin1String(value));
 add(set,p+QStringLiteral(".unit"),QStringLiteral("millimeters"));add(set,p+QStringLiteral(".precision"),QStringLiteral("2"));
 if(from)add(set,p+QStringLiteral(".Leader[0].terminator"),QLatin1String(from));
 if(to)add(set,p+QStringLiteral(".Leader[1].terminator"),QLatin1String(to));
 add(set,p+QStringLiteral(".textOrigin"),QStringLiteral("42 -50 -39"));
}
}
void recordedPmiTests(){
 using namespace RecordedPmi;
 // Vector parsing is strict.
 QVector3D v;
 check(parseVector(QStringLiteral("1 -2 3.5"),v) && v==QVector3D(1,-2,3.5f),"Three numbers parse");
 check(parseVector(QStringLiteral("  1   2\t3 "),v) && v==QVector3D(1,2,3),"Extra whitespace is tolerated");
 check(!parseVector(QStringLiteral("1 2"),v) && !parseVector(QStringLiteral("a b c"),v) && !parseVector(QStringLiteral("1 2 3 4"),v) && !parseVector(QStringLiteral("1 2 nan"),v) && !parseVector(QString(),v),"Malformed vectors are rejected");

 // Only complete linear dimensions are taken, with multi-digit entity numbers kept in order.
 NodePropertySet set;set.nodeName="part";
 add(set,QStringLiteral("JTRepresentation"),QStringLiteral("StoredTessellation"));
 dimension(set,0,"Dimension","1","100","100 -50 0","0 -50 0");
 dimension(set,1,"Dimension","3","25","35.9 -50 21.8",nullptr);          // radial: left for later
 dimension(set,2,"Dimension","1","25","75 -50 25",nullptr);               // one terminator missing
 dimension(set,3,"Note","1","25","75 -50 25","100 -50 25");               // not a dimension
 dimension(set,4,"Dimension","1",nullptr,"75 -50 25","100 -50 25");       // no value
 dimension(set,10,"Dimension","1","24.99999999999999","75 -50 25","100 -50 25");
 const auto dims=linearDimensions(set);
 check(dims.size()==2,"Two complete linear dimensions");
 check(dims[0].entity==0 && near(dims[0].value,100) && dims[0].unit=="millimeters" && dims[0].precision==2 && dims[0].from==QVector3D(100,-50,0) && dims[0].to==QVector3D(0,-50,0) && dims[0].hasTextOrigin && dims[0].textOrigin==QVector3D(42,-50,-39),"First dimension fields");
 check(dims[1].entity==10 && near(dims[1].value,25,1e-9) && dims[1].to==QVector3D(100,-50,25),"Entity numbers above nine and the stored value are kept");
 NodePropertySet none;add(none,QStringLiteral("JTRepresentation"),QStringLiteral("Assembly"));
 check(linearDimensions(none).empty(),"A node without PMI rows has no dimensions");

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
}
