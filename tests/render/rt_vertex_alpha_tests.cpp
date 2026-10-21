// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original overlapping triangles exercise actual Embree hits and CPU rendering.
#include "CpuPathTracer.h"
#include "RtEmbreeScene.h"
#include <QCoreApplication>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
RtMeshGeometry triangle(float z,const glm::vec3& rgb,float a,float b,float c){
    RtMeshGeometry mesh;
    const glm::vec3 p[]={{-10,-10,z},{10,-10,z},{0,10,z}};
    const float alpha[]={a,b,c};
    for(unsigned i=0;i<3;++i){RtVertex v;v.position=p[i];v.normal={0,0,1};v.color=glm::vec4(rgb,alpha[i]);mesh.vertices.push_back(v);}
    mesh.indices={0,1,2};return mesh;
}
void coverage(int mode,float a,float b,float c,float expected,float scalar=1.f){
    auto snapshot=std::make_shared<RtSceneSnapshot>();
    snapshot->camera.position={0,0,3};snapshot->camera.orthographic=true;snapshot->camera.orthoHalfHeight=0.1f;
    snapshot->shadowsEnabled=false;
    snapshot->meshes.push_back(triangle(0,{0,0,1},a,b,c));
    snapshot->meshes.push_back(triangle(-1,{1,0.5f,0},1,1,1));
    RtMaterial outer;outer.baseColor={1,1,1};outer.blendMode=mode;outer.opacity=scalar;outer.alphaThreshold=0.5f;
    RtMaterial inner;inner.baseColor={1,1,1};
    snapshot->materials={outer,inner};
    RtInstance front;RtInstance back;back.meshIndex=1;back.materialIndex=1;snapshot->instances={front,back};
    RtEmbreeScene scene;check(scene.build(snapshot),"Cannot build original alpha scene");
    RtRay ray;ray.origin={0,0,3};ray.direction={0,0,-1};const auto hit=scene.intersect(ray);
    check(hit.hit&&std::abs(hit.vertexColor.a-(0.25f*a+0.25f*b+0.5f*c))<1e-5f,"Embree lost interpolated alpha");
    CpuPathTracer tracer;RtEnvironmentSampler environment;environment.build(snapshot->environment);
    std::vector<glm::vec3> radiance,guides;
    tracer.renderPass(scene,*snapshot,environment,64,64,12345,radiance,nullptr,nullptr,&guides);
    check(guides.size()==4096,"CPU alpha guide count changed");
    unsigned blue=0;
    for(const auto& value:guides){
        const bool frontHit=glm::length(value-glm::vec3(0,0,1))<1e-4f;
        const bool backHit=glm::length(value-glm::vec3(1,0.5f,0))<1e-4f;
        check(frontHit||backHit,"CPU alpha did not resolve an original foreground/background surface");blue+=frontHit;
    }
    const float observed=float(blue)/float(guides.size());
    check(std::abs(observed-expected)<(expected==0||expected==1?1e-6f:0.06f),"CPU vertex-alpha coverage disagrees with known probability");
}
}
int main(int argc,char** argv){
    QCoreApplication application(argc,argv);
    try{
        coverage(0,0,0,0,1); // OPAQUE ignores vertex alpha
        coverage(2,0,0,0,0);coverage(2,1,1,1,1);coverage(2,0.5f,0.5f,0.5f,0.5f);
        coverage(2,0,0,1,0.5f); // barycentric alpha, rather than one corner's value
        coverage(2,0.5f,0.5f,0.5f,0.25f,0.5f); // scalar and vertex coverage multiply once
        coverage(1,0.49f,0.49f,0.49f,0);coverage(1,0.5f,0.5f,0.5f,1);
        std::cout<<"CPU ray-traced vertex-alpha endpoints, interpolation, coverage, scalar and MASK tests passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
