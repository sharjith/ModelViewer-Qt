// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <JtData_Limits.hxx>
#include <assimp/mesh.h>
#include <array>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <vector>

namespace Jt::TextureClampMesh {
// Exact piecewise affine UV clamp for equal, non-mip min/mag filters. Splitting
// at the two UV boundaries avoids clamping vertices of an unsplit triangle,
// which would change its interior interpolation. Mip derivatives are NOT
// preserved outside the interval, so mipmapped legacy CLAMP remains rejected.
template<class Poll>
std::unique_ptr<aiMesh> apply(aiMesh& mesh,unsigned channel,bool clampS,bool clampT,
                            std::array<float,4> bounds,Poll poll) {
    if(channel>=4||!mesh.HasTextureCoords(channel)||mesh.HasBones())
        throw std::runtime_error("legacy clamp needs an unskinned UV0-3 triangle mesh");
    const auto clampUV=[&](aiVector3D& uv){
        if(clampS)uv.x=std::clamp(uv.x,bounds[0],bounds[1]);
        if(clampT)uv.y=std::clamp(uv.y,bounds[2],bounds[3]);
    };
    const auto crosses=[&](const aiFace& face,unsigned axis,float boundary){
        float lo=mesh.mTextureCoords[channel][face.mIndices[0]][axis],hi=lo;
        for(unsigned i=1;i<3;++i){const float v=mesh.mTextureCoords[channel][face.mIndices[i]][axis];lo=std::min(lo,v);hi=std::max(hi,v);}
        return lo<boundary&&hi>boundary;
    };
    bool split=false;
    for(unsigned f=0;f<mesh.mNumFaces&&!split;++f){
        if((f&1023)==0)poll();const auto& face=mesh.mFaces[f];
        split=(clampS&&(crosses(face,0,bounds[0])||crosses(face,0,bounds[1])))
            ||(clampT&&(crosses(face,1,bounds[2])||crosses(face,1,bounds[3])));
    }
    if(!split){
        for(unsigned i=0;i<mesh.mNumVertices;++i){if((i&1023)==0)poll();clampUV(mesh.mTextureCoords[channel][i]);}
        return {};
    }
    struct Vertex {aiVector3D position,normal;aiColor4D color;std::array<aiVector3D,4> uv;};
    struct Polygon {unsigned count=0;std::array<Vertex,8> vertices;};
    std::vector<Vertex> vertices;
    const auto append=[&](const Vertex& v){
        if(vertices.size()>=4u*1024u*1024u)throw std::runtime_error("legacy clamp refinement exceeds vertex limit");
        if(vertices.size()==vertices.capacity()){
            const size_t next=std::min<size_t>(4u*1024u*1024u,std::max<size_t>(64,vertices.capacity()*2));
            JtData_Limits::allocation(next*sizeof(Vertex));vertices.reserve(next);
        }
        vertices.push_back(v);
    };
    const auto read=[&](unsigned i){
        Vertex v;v.position=mesh.mVertices[i];if(mesh.HasNormals())v.normal=mesh.mNormals[i];
        if(mesh.HasVertexColors(0))v.color=mesh.mColors[0][i];
        for(unsigned c=0;c<4;++c)if(mesh.HasTextureCoords(c))v.uv[c]=mesh.mTextureCoords[c][i];
        return v;
    };
    const auto interpolate=[&](const Vertex& a,const Vertex& b,double t){
        Vertex v;const auto mix=[&](float x,float y){return float(double(x)*(1-t)+double(y)*t);};
        for(unsigned c=0;c<3;++c){v.position[c]=mix(a.position[c],b.position[c]);v.normal[c]=mix(a.normal[c],b.normal[c]);}
        v.color=aiColor4D(mix(a.color.r,b.color.r),mix(a.color.g,b.color.g),mix(a.color.b,b.color.b),mix(a.color.a,b.color.a));
        for(unsigned u=0;u<4;++u)for(unsigned c=0;c<3;++c)v.uv[u][c]=mix(a.uv[u][c],b.uv[u][c]);
        if(mesh.HasNormals()&&v.normal.SquareLength()<0.1f)
            throw std::runtime_error("legacy clamp refinement encounters opposing/degenerate normals");
        return v;
    };
    const auto hasArea=[](const aiVector3D& a,const aiVector3D& b,const aiVector3D& c){
        const double x1=double(b.x)-a.x,y1=double(b.y)-a.y,z1=double(b.z)-a.z;
        const double x2=double(c.x)-a.x,y2=double(c.y)-a.y,z2=double(c.z)-a.z;
        return y1*z2-z1*y2!=0||z1*x2-x1*z2!=0||x1*y2-y1*x2!=0;
    };
    std::array<Polygon,16> polygons,next;
    for(unsigned f=0;f<mesh.mNumFaces;++f){
        poll();unsigned count=1;polygons[0].count=3;
        for(unsigned i=0;i<3;++i)polygons[0].vertices[i]=read(mesh.mFaces[f].mIndices[i]);
        if(!hasArea(polygons[0].vertices[0].position,polygons[0].vertices[1].position,polygons[0].vertices[2].position))
            throw std::runtime_error("legacy clamp refinement requires nondegenerate source triangles");
        for(unsigned cut=0;cut<4;++cut){
            const unsigned axis=cut/2;if((axis==0&&!clampS)||(axis==1&&!clampT))continue;
            const float boundary=bounds[cut];unsigned nextCount=0;
            for(unsigned p=0;p<count;++p){
                const auto& in=polygons[p];bool low=false,high=false;
                for(unsigned i=0;i<in.count;++i){const float u=in.vertices[i].uv[channel][axis];low=low||u<boundary;high=high||u>boundary;}
                if(!(low&&high)){if(nextCount>=next.size())throw std::runtime_error("legacy clamp polygon limit");next[nextCount++]=in;continue;}
                if(nextCount+2>next.size())throw std::runtime_error("legacy clamp polygon limit");
                auto& below=next[nextCount++];auto& above=next[nextCount++];below.count=above.count=0;
                const auto put=[](Polygon& out,const Vertex& v){if(out.count==out.vertices.size())throw std::runtime_error("legacy clamp corner limit");out.vertices[out.count++]=v;};
                for(unsigned i=0;i<in.count;++i){
                    const auto& a=in.vertices[i];const auto& b=in.vertices[(i+1)%in.count];
                    const double u=a.uv[channel][axis],v=b.uv[channel][axis];
                    if(u<=boundary)put(below,a);if(u>=boundary)put(above,a);
                    if((u<boundary&&v>boundary)||(u>boundary&&v<boundary)){
                        auto edge=interpolate(a,b,(boundary-u)/(v-u));edge.uv[channel][axis]=boundary;
                        put(below,edge);put(above,edge);
                    }
                }
            }
            polygons.swap(next);count=nextCount;
        }
        for(unsigned p=0;p<count;++p)for(unsigned i=1;i+1<polygons[p].count;++i){
            const auto& a=polygons[p].vertices[0].position;
            const auto& b=polygons[p].vertices[i].position;
            const auto& c=polygons[p].vertices[i+1].position;
            // A collinear clipping corner need not produce a zero-area fan
            // triangle (nor a new degenerate flat-normal calculation).
            if(!hasArea(a,b,c))continue;
            for(unsigned corner:{0u,i,i+1}){auto v=polygons[p].vertices[corner];clampUV(v.uv[channel]);append(v);}
        }
    }
    if(vertices.empty())throw std::runtime_error("legacy clamp refinement produced no nondegenerate triangles");
    auto out=std::make_unique<aiMesh>();out->mPrimitiveTypes=aiPrimitiveType_TRIANGLE;
    out->mNumVertices=unsigned(vertices.size());out->mNumFaces=out->mNumVertices/3;
    JtData_Limits::allocation(vertices.size()*sizeof(aiVector3D)*(1+unsigned(mesh.HasNormals()))
        +vertices.size()*sizeof(aiColor4D)*unsigned(mesh.HasVertexColors(0))
        +size_t(out->mNumFaces)*(sizeof(aiFace)+3*sizeof(unsigned)));
    out->mVertices=new aiVector3D[out->mNumVertices];if(mesh.HasNormals())out->mNormals=new aiVector3D[out->mNumVertices];
    if(mesh.HasVertexColors(0))out->mColors[0]=new aiColor4D[out->mNumVertices];
    for(unsigned u=0;u<4;++u)if(mesh.HasTextureCoords(u)){
        JtData_Limits::allocation(vertices.size()*sizeof(aiVector3D));out->mTextureCoords[u]=new aiVector3D[out->mNumVertices];out->mNumUVComponents[u]=mesh.mNumUVComponents[u];
    }
    for(unsigned i=0;i<out->mNumVertices;++i){
        if((i&1023)==0)poll();const auto& v=vertices[i];out->mVertices[i]=v.position;
        if(out->HasNormals())out->mNormals[i]=v.normal;if(out->HasVertexColors(0))out->mColors[0][i]=v.color;
        for(unsigned u=0;u<4;++u)if(out->HasTextureCoords(u))out->mTextureCoords[u][i]=v.uv[u];
    }
    out->mFaces=new aiFace[out->mNumFaces];
    for(unsigned f=0;f<out->mNumFaces;++f){if((f&1023)==0)poll();auto& face=out->mFaces[f];face.mNumIndices=3;face.mIndices=new unsigned[3]{3*f,3*f+1,3*f+2};}
    return out;
}
}
