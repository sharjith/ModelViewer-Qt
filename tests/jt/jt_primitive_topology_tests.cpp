// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original literal in-memory tests of the primitive-kind aware topology helpers (C3 stage B). They use hand-built scene
// meshes only; they are not evidence about any authentic polyline, point or wireframe JT file, and no JT decoder path
// produces line or point meshes yet.
#include "JtPrimitiveTopology.h"
#include <assimp/mesh.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F action,const char* message){
 try{action();}catch(const std::runtime_error& error){check(std::string(error.what())==message,message);return;}
 throw std::runtime_error(std::string("Expected rejection: ")+message);
}
std::unique_ptr<aiMesh> makeMesh(unsigned kind,unsigned vertices,const std::vector<std::vector<unsigned>>& faces){
 auto mesh=std::make_unique<aiMesh>();
 mesh->mPrimitiveTypes=kind;mesh->mNumVertices=vertices;mesh->mVertices=new aiVector3D[vertices];
 for(unsigned i=0;i<vertices;++i)mesh->mVertices[i]=aiVector3D(float(i),0,0);
 mesh->mNumFaces=unsigned(faces.size());mesh->mFaces=new aiFace[faces.size()];
 for(size_t i=0;i<faces.size();++i){
  mesh->mFaces[i].mNumIndices=unsigned(faces[i].size());mesh->mFaces[i].mIndices=new unsigned[faces[i].size()];
  for(size_t j=0;j<faces[i].size();++j)mesh->mFaces[i].mIndices[j]=faces[i][j];
 }
 return mesh;
}
std::unique_ptr<aiMesh> emptyTarget(unsigned faces){
 auto mesh=std::make_unique<aiMesh>();mesh->mNumFaces=faces;mesh->mFaces=new aiFace[faces];return mesh;
}
}
void primitiveTopologyTests(){
 using namespace Jt::Detail;
 // Kind to index count; an unset kind is a triangle mesh, several kinds or polygons are not accepted.
 check(primitiveIndexCount(aiPrimitiveType_POINT)==1 && primitiveIndexCount(aiPrimitiveType_LINE)==2 && primitiveIndexCount(aiPrimitiveType_TRIANGLE)==3,"Index count per primitive kind");
 check(primitiveIndexCount(aiPrimitiveType_POINT|aiPrimitiveType_LINE)==0 && primitiveIndexCount(aiPrimitiveType_POLYGON)==0,"Mixed kinds and polygons must have no index count");
 auto legacy=makeMesh(0,3,{{0,1,2}});
 check(meshIndexCount(*legacy)==3 && isTriangleMesh(*legacy),"A mesh without a kind flag is a triangle mesh");

 // Triangle meshes keep the messages the importer always used.
 validateFaceTopology(*makeMesh(aiPrimitiveType_TRIANGLE,4,{{0,1,2},{1,2,3}}));
 rejects([]{validateFaceTopology(*makeMesh(aiPrimitiveType_TRIANGLE,3,{{0,1}}));},"JT has a non-triangle render primitive");
 rejects([]{validateFaceTopology(*makeMesh(aiPrimitiveType_TRIANGLE,3,{{0,1,3}}));},"JT triangle index out of range");

 // Line segments: two indices per face, disconnected polylines stay disconnected (no segment joins 2 and 3).
 auto lines=makeMesh(aiPrimitiveType_LINE,5,{{0,1},{1,2},{3,4}});
 validateFaceTopology(*lines);
 check(!isTriangleMesh(*lines) && meshIndexCount(*lines)==2,"A line mesh is not a triangle mesh");
 rejects([]{validateFaceTopology(*makeMesh(aiPrimitiveType_LINE,5,{{0,1,2}}));},"JT has a render primitive that is not a line segment");
 rejects([]{validateFaceTopology(*makeMesh(aiPrimitiveType_LINE,5,{{0,5}}));},"JT line segment index out of range");

 // Points: one index per face.
 validateFaceTopology(*makeMesh(aiPrimitiveType_POINT,6,{{0},{1},{2},{3},{4},{5}}));
 rejects([]{validateFaceTopology(*makeMesh(aiPrimitiveType_POINT,2,{{0,1}}));},"JT has a render primitive that is not a point");
 rejects([]{validateFaceTopology(*makeMesh(aiPrimitiveType_POINT,2,{{2}}));},"JT point index out of range");
 rejects([]{validateFaceTopology(*makeMesh(aiPrimitiveType_POINT|aiPrimitiveType_LINE,2,{{0}}));},"JT mesh does not hold exactly one primitive kind");

 // Copy keeps the face boundaries exactly.
 auto copy=emptyTarget(lines->mNumFaces);
 copyFaces(*lines,*copy);
 check(copy->mFaces[0].mNumIndices==2 && copy->mFaces[2].mNumIndices==2,"Copied line faces keep two indices");
 check(copy->mFaces[1].mIndices[0]==1 && copy->mFaces[1].mIndices[1]==2 && copy->mFaces[2].mIndices[0]==3 && copy->mFaces[2].mIndices[1]==4,"Copied line indices");
 auto points=makeMesh(aiPrimitiveType_POINT,3,{{2},{0},{1}});
 auto pointCopy=emptyTarget(3);
 copyFaces(*points,*pointCopy);
 check(pointCopy->mFaces[0].mNumIndices==1 && pointCopy->mFaces[0].mIndices[0]==2 && pointCopy->mFaces[2].mIndices[0]==1,"Copied point faces keep one index");

 // Subset: the chosen lines, their vertices renumbered into the sorted used-vertex list, in the order given.
 const std::vector<unsigned> chosen{2,0};
 const auto used=usedVertices(*lines,chosen);
 check(used==std::vector<unsigned>({0,1,3,4}),"Used vertices of a line subset");
 auto subset=emptyTarget(unsigned(chosen.size()));
 unsigned polls=0;
 remapFaces(*lines,chosen,used,*subset,[&]{++polls;});
 check(polls==2,"remapFaces must poll once per face");
 check(subset->mFaces[0].mNumIndices==2 && subset->mFaces[0].mIndices[0]==2 && subset->mFaces[0].mIndices[1]==3,"Renumbered second polyline");
 check(subset->mFaces[1].mIndices[0]==0 && subset->mFaces[1].mIndices[1]==1,"Renumbered first polyline segment");
 // The same helpers on triangles reproduce the old behaviour.
 auto triangles=makeMesh(aiPrimitiveType_TRIANGLE,5,{{0,1,2},{2,3,4}});
 const auto triangleUsed=usedVertices(*triangles,{1});
 check(triangleUsed==std::vector<unsigned>({2,3,4}),"Used vertices of a triangle subset");
 auto triangleSubset=emptyTarget(1);
 remapFaces(*triangles,{1},triangleUsed,*triangleSubset,[]{});
 check(triangleSubset->mFaces[0].mNumIndices==3 && triangleSubset->mFaces[0].mIndices[0]==0 && triangleSubset->mFaces[0].mIndices[2]==2,"Renumbered triangle");
}
