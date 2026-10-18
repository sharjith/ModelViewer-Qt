#include "Jt/JtImporter.h"
#include <QCoreApplication>
#include <assimp/Exporter.hpp>
#include <iostream>
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    if(argc<2) return 2;
    try {
        auto result=Jt::Importer::read(QString::fromLocal8Bit(argv[1]));
        unsigned vertices=0,triangles=0;
        for(unsigned i=0;i<result.scene->mNumMeshes;++i) {vertices+=result.scene->mMeshes[i]->mNumVertices;triangles+=result.scene->mMeshes[i]->mNumFaces;}
        std::cout<<"meshes="<<result.scene->mNumMeshes<<" vertices="<<vertices<<" triangles="<<triangles<<" fallbackParts="<<result.fallbackParts<<'\n';
        for(const auto& warning:result.warnings) std::cout<<warning.toStdString()<<'\n';
        if(argc==3) {Assimp::Exporter exporter;if(exporter.Export(result.scene.get(),"glb2",argv[2])!=AI_SUCCESS) throw std::runtime_error(exporter.GetErrorString());}
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
    return 0;
}
