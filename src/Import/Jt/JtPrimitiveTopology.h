// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original ModelViewer-Qt helpers (C3 stage B, 2026-10-31): primitive-kind aware face topology for JT scene meshes.
// A scene mesh holds one primitive kind: points (1 index), line segments (2) or triangles (3). Triangle behaviour,
// messages and limits are unchanged; the line and point kinds have no producer yet (see
// research/jt/JT_DISPLAY_PRIMITIVES_AUDIT.md) and are only prepared here.
#pragma once
#include <assimp/mesh.h>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>
namespace Jt::Detail {
// Indices per primitive for a mesh of one primitive kind: 1 point, 2 line segment, 3 triangle; 0 for anything else
// (several kinds, polygons, or no kind set).
inline unsigned primitiveIndexCount(unsigned primitiveTypes) {
    switch(primitiveTypes) {
    case aiPrimitiveType_POINT: return 1;
    case aiPrimitiveType_LINE: return 2;
    case aiPrimitiveType_TRIANGLE: return 3;
    default: return 0;
    }
}
// Meshes built before primitive kinds were distinguished never set the flag; they are triangle meshes.
inline unsigned meshIndexCount(const aiMesh& mesh) {
    return mesh.mPrimitiveTypes==0 ? 3u : primitiveIndexCount(mesh.mPrimitiveTypes);
}
inline const char* primitiveName(unsigned indices) {
    return indices==1 ? "point" : indices==2 ? "line segment" : "triangle";
}
// Throws unless every face has exactly the mesh's index count and only indexes existing vertices.
// The messages for triangle meshes are the ones the importer always used.
inline void validateFaceTopology(const aiMesh& mesh) {
    const unsigned count=meshIndexCount(mesh);
    if(!count) throw std::runtime_error("JT mesh does not hold exactly one primitive kind");
    const std::string name=primitiveName(count);
    for(unsigned i=0;i<mesh.mNumFaces;++i) {
        const auto& face=mesh.mFaces[i];
        if(face.mNumIndices!=count || !face.mIndices)
            throw std::runtime_error(count==3 ? std::string("JT has a non-triangle render primitive") : "JT has a render primitive that is not a "+name);
        for(unsigned j=0;j<count;++j)
            if(face.mIndices[j]>=mesh.mNumVertices) throw std::runtime_error("JT "+name+" index out of range");
    }
}
// Copies the faces of `source` (one primitive kind) unchanged into `target`, which must already hold mNumFaces faces.
inline void copyFaces(const aiMesh& source,aiMesh& target) {
    const unsigned count=meshIndexCount(source);
    if(!count) throw std::runtime_error("JT mesh does not hold exactly one primitive kind");
    for(unsigned i=0;i<source.mNumFaces;++i) {
        auto& face=target.mFaces[i];
        face.mNumIndices=count;face.mIndices=new unsigned[count];
        std::copy_n(source.mFaces[i].mIndices,count,face.mIndices);
    }
}
// Sorted unique vertex indices used by the given faces of `source`.
inline std::vector<unsigned> usedVertices(const aiMesh& source,const std::vector<unsigned>& faces) {
    const unsigned count=meshIndexCount(source);
    std::vector<unsigned> vertices;
    vertices.reserve(faces.size()*count);
    for(auto face:faces) for(unsigned j=0;j<count;++j) vertices.push_back(source.mFaces[face].mIndices[j]);
    std::sort(vertices.begin(),vertices.end());
    vertices.erase(std::unique(vertices.begin(),vertices.end()),vertices.end());
    return vertices;
}
// Fills `target` (already holding faces.size() faces) with the chosen faces of `source`, their indices renumbered into the
// sorted vertex list returned by usedVertices().
// `poll` is called once per face so a long copy stays cancellable.
template<class Poll>
inline void remapFaces(const aiMesh& source,const std::vector<unsigned>& faces,const std::vector<unsigned>& vertices,aiMesh& target,Poll poll) {
    const unsigned count=meshIndexCount(source);
    for(unsigned i=0;i<faces.size();++i) {
        poll();
        auto& face=target.mFaces[i];
        face.mNumIndices=count;face.mIndices=new unsigned[count];
        for(unsigned j=0;j<count;++j)
            face.mIndices[j]=unsigned(std::lower_bound(vertices.begin(),vertices.end(),source.mFaces[faces[i]].mIndices[j])-vertices.begin());
    }
}
// True for the one kind the merge, flat-normal and tangent steps understand: they read three indices per face.
inline bool isTriangleMesh(const aiMesh& mesh) {return meshIndexCount(mesh)==3;}
}
