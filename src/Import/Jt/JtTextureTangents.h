// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <JtData_Limits.hxx>
#include <assimp/mesh.h>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

namespace Jt::TextureTangents {
struct Vec {
    double x=0,y=0,z=0;
    Vec operator+(Vec b)const{return {x+b.x,y+b.y,z+b.z};}
    Vec operator-(Vec b)const{return {x-b.x,y-b.y,z-b.z};}
    Vec operator*(double s)const{return {x*s,y*s,z*s};}
};
inline Vec from(const aiVector3D& p){return {p.x,p.y,p.z};}
inline double dot(Vec a,Vec b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vec cross(Vec a,Vec b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline Vec unit(Vec a){
    const double length=std::hypot(a.x,a.y,a.z);
    if(!std::isfinite(length)||length<1e-20)throw std::runtime_error("normal map requires a finite nondegenerate tangent basis");
    return a*(1/length);
}
template<class Poll>
void apply(aiMesh& mesh,Poll poll){
    if(!mesh.HasTextureCoords(0)||!mesh.HasNormals())throw std::runtime_error("normal map requires stored UV0 and normals");
    JtData_Limits::allocation(size_t(mesh.mNumVertices)*(2*sizeof(Vec)+2*sizeof(aiVector3D)));
    std::vector<Vec> tangents(mesh.mNumVertices),bitangents(mesh.mNumVertices);
    for(unsigned i=0;i<mesh.mNumFaces;++i){
        if((i&1023)==0)poll();const auto& face=mesh.mFaces[i];
        if(face.mNumIndices!=3)throw std::runtime_error("normal map tangent profile requires triangles");
        for(unsigned j=0;j<3;++j)if(face.mIndices[j]>=mesh.mNumVertices)throw std::runtime_error("normal map triangle index is out of range");
        const unsigned a=face.mIndices[0],b=face.mIndices[1],c=face.mIndices[2];
        const Vec e1=from(mesh.mVertices[b])-from(mesh.mVertices[a]),e2=from(mesh.mVertices[c])-from(mesh.mVertices[a]);
        const auto& ua=mesh.mTextureCoords[0][a];const auto& ub=mesh.mTextureCoords[0][b];const auto& uc=mesh.mTextureCoords[0][c];
        const double s1=double(ub.x)-ua.x,t1=double(ub.y)-ua.y,s2=double(uc.x)-ua.x,t2=double(uc.y)-ua.y;
        const double determinant=s1*t2-s2*t1;
        if(!std::isfinite(determinant)||std::abs(determinant)<1e-20)throw std::runtime_error("normal map has a degenerate UV0 triangle");
        const Vec t=(e1*t2-e2*t1)*(1/determinant),bit=(e2*s1-e1*s2)*(1/determinant);
        for(unsigned index:{a,b,c}){
            if(dot(cross(t,bit),unit(from(mesh.mNormals[index])))<=0)
                throw std::runtime_error("mirrored or inverted normal-map tangent charts are not qualified");
            tangents[index]=tangents[index]+t;bitangents[index]=bitangents[index]+bit;
        }
    }
    auto outT=std::make_unique<aiVector3D[]>(mesh.mNumVertices),outB=std::make_unique<aiVector3D[]>(mesh.mNumVertices);
    for(unsigned i=0;i<mesh.mNumVertices;++i){
        if((i&1023)==0)poll();const Vec n=unit(from(mesh.mNormals[i]));
        const Vec t=unit(tangents[i]-n*dot(n,tangents[i])),b=unit(cross(n,t));
        if(dot(b,bitangents[i])<=0)throw std::runtime_error("normal-map tangent accumulation is inconsistent");
        outT[i]={float(t.x),float(t.y),float(t.z)};outB[i]={float(b.x),float(b.y),float(b.z)};
    }
    delete[] mesh.mTangents;delete[] mesh.mBitangents;
    mesh.mTangents=outT.release();mesh.mBitangents=outB.release();
}
}
