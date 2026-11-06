// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <JtData_Limits.hxx>
#include <assimp/mesh.h>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace Jt::TextureMapping {
// A standard UV attribute interpolates the divided values. Equivalence to
// homogeneous interpolation/division is guaranteed here by constant face Q.
// Do not approximate varying Q by dividing only at the vertices.
template<class Poll>
std::vector<aiVector3D> map(const aiMesh& mesh,unsigned channel,
    const std::array<float,16>& transform,const std::array<int,4>& generation,
    const std::array<float,16>& planes,const std::array<double,4>& imageMapping,Poll poll) {
    if(channel>=4)throw std::runtime_error("texture coordinate channel is outside UV0-3");
    const auto* stored=mesh.mTextureCoords[channel];
    if(!stored&&(generation[0]==0||generation[1]==0))
        throw std::runtime_error("selected UV channel is absent and S/T are not both generated");
    for(unsigned c=0;c<4;++c){
        if(generation[c]<0||generation[c]>1)
            throw std::runtime_error("view-dependent texture coordinate generation is not represented by portable UVs");
        if(generation[c]==1){
            const auto* p=planes.data()+4*c;
            const double norm=double(p[0])*p[0]+double(p[1])*p[1]+double(p[2])*p[2];
            if(!std::isfinite(p[3])||!std::isfinite(norm)||std::abs(norm-1.0)>1e-5)
                throw std::runtime_error("model-linear texture reference plane requires a finite unit normal");
        }
    }
    for(float value:transform)if(!std::isfinite(value))
        throw std::runtime_error("nonfinite texture transform");
    // Prove every input that contributes to Q separately constant. Checking
    // only evaluated Q values could hide tiny variation in a large offset's
    // rounding. This sufficient proof deliberately rejects cancellation-only
    // cases, including oblique planes constant only by cancelling axis terms.
    for(unsigned i=0;i<mesh.mNumFaces;++i){
        if((i&1023)==0)poll();
        const auto& face=mesh.mFaces[i];
        if(face.mNumIndices!=3)throw std::runtime_error("portable homogeneous texture mapping requires triangles");
        for(unsigned j=0;j<3;++j)if(face.mIndices[j]>=mesh.mNumVertices)
            throw std::runtime_error("texture mapping triangle index is out of range");
        const auto a=face.mIndices[0],b=face.mIndices[1],c=face.mIndices[2];
        for(unsigned input=0;input<4;++input){
            if(transform[4*input+3]==0)continue;
            bool constant=true;
            if(generation[input]==1){
                for(unsigned axis=0;axis<3;++axis)if(planes[4*input+axis]!=0)
                    constant=constant&&mesh.mVertices[a][axis]==mesh.mVertices[b][axis]
                        &&mesh.mVertices[a][axis]==mesh.mVertices[c][axis];
            }else if(input<2){
                constant=stored[a][input]==stored[b][input]&&stored[a][input]==stored[c][input];
            }
            if(!constant)throw std::runtime_error("texture homogeneous denominator is not proven constant on each triangle");
        }
    }
    JtData_Limits::allocation(size_t(mesh.mNumVertices)*sizeof(aiVector3D));
    std::vector<aiVector3D> mapped(mesh.mNumVertices);
    for(unsigned i=0;i<mesh.mNumVertices;++i){
        if((i&1023)==0)poll();
        std::array<double,4> input{stored?double(stored[i].x):0,stored?double(stored[i].y):0,0,1};
        for(unsigned c=0;c<4;++c)if(generation[c]==1){
            const auto* plane=planes.data()+4*c;const auto& p=mesh.mVertices[i];
            input[c]=double(plane[0])*p.x+double(plane[1])*p.y+double(plane[2])*p.z+plane[3];
        }
        // Serialized row-vector convention: translation is the last row.
        const auto component=[&](unsigned c){
            return input[0]*transform[c]+input[1]*transform[4+c]
                +input[2]*transform[8+c]+input[3]*transform[12+c];
        };
        const double q=component(3);
        if(!std::isfinite(q)||q==0)
            throw std::runtime_error("texture homogeneous denominator is zero or nonfinite");
        const double s=(component(0)/q)*imageMapping[0]+imageMapping[2];
        const double t=(component(1)/q)*imageMapping[1]+imageMapping[3];
        if(!std::isfinite(s)||!std::isfinite(t)||std::abs(s)>std::numeric_limits<float>::max()
            ||std::abs(t)>std::numeric_limits<float>::max())
            throw std::runtime_error("transformed UV exceeds finite render range");
        mapped[i]={float(s),float(t),0};
    }
    return mapped;
}
}
