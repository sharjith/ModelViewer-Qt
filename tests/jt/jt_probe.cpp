#include "Jt/JtImporter.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <assimp/Exporter.hpp>
#include <iostream>
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    QCommandLineParser parser;parser.addHelpOption();
    parser.addPositionalArgument("input","JT file to inspect");
    parser.addPositionalArgument("output","Optional GLB export","[output.glb]");
    const QCommandLineOption eye("eye","Import-time RangeLOD eye position in JT world coordinates / source units","x,y,z");
    parser.addOption(eye);parser.process(app);
    const auto arguments=parser.positionalArguments();
    if(arguments.size()<1 || arguments.size()>2) parser.showHelp(2);
    try {
        Jt::Options options;
        if(parser.isSet(eye)) {
            const auto coordinates=parser.value(eye).split(',');
            if(coordinates.size()!=3) throw std::runtime_error("--eye requires x,y,z");
            std::array<double,3> point{};
            for(int i=0;i<3;++i) {bool ok=false;point[i]=coordinates[i].toDouble(&ok);if(!ok) throw std::runtime_error("Invalid --eye coordinate");}
            options.lodEyePoint=point;
        }
        auto result=Jt::Importer::read(arguments[0],options);
        unsigned vertices=0,triangles=0;
        for(unsigned i=0;i<result.scene->mNumMeshes;++i) {vertices+=result.scene->mMeshes[i]->mNumVertices;triangles+=result.scene->mMeshes[i]->mNumFaces;}
        std::cout<<"meshes="<<result.scene->mNumMeshes<<" vertices="<<vertices<<" triangles="<<triangles<<" fallbackParts="<<result.fallbackParts<<'\n';
        for(const auto& warning:result.warnings) std::cout<<warning.toStdString()<<'\n';
        std::function<void(const aiNode*)> selections=[&](const aiNode* node) {
            int32_t selected=-1;double distance=0;
            if(node->mMetaData && node->mMetaData->Get("JTSelectedChild",selected)) {
                std::cout<<"selection="<<node->mName.C_Str()<<" child="<<selected;
                if(node->mMetaData->Get("JTRangeLODDistance",distance)) std::cout<<" distance="<<distance;
                std::cout<<'\n';
            }
            for(unsigned i=0;i<node->mNumChildren;++i) selections(node->mChildren[i]);
        };selections(result.scene->mRootNode);
        if(arguments.size()==2) {Assimp::Exporter exporter;if(exporter.Export(result.scene.get(),"glb2",arguments[1].toLocal8Bit().constData())!=AI_SUCCESS) throw std::runtime_error(exporter.GetErrorString());}
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
    return 0;
}
