// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original containers; the caller supplies the local, non-bundled geometry sample.
#include "JtContainerFixture.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <iostream>

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);QCommandLineParser parser;parser.addHelpOption();
    const QCommandLineOption samples("samples","Folder containing a locally obtained Bolt.jt","folder");
    const QCommandLineOption output("output","Output folder for generated JT fixtures","folder");
    const QCommandLineOption kind("kind","Fixture set: metadata or draw-style","set","metadata");
    parser.addOption(samples);parser.addOption(output);parser.addOption(kind);parser.process(app);
    if(!parser.isSet(samples) || !parser.isSet(output)) parser.showHelp(2);
    try {
        const auto set=parser.value(kind);
        if(set!="metadata" && set!="draw-style") throw std::runtime_error("Unknown fixture set; use metadata or draw-style");
        const QDir out(parser.value(output));
        QFile source(QDir(parser.value(samples)).filePath("Bolt.jt"));
        if(!source.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot read locally supplied Bolt.jt");
        const auto bytes=source.readAll();
        if(!QDir().mkpath(out.filePath("parts"))) throw std::runtime_error("Cannot create fixture directory");
        QFile target(out.filePath("parts/Bolt.jt"));
        if(target.exists()) {
            if(!target.open(QIODevice::ReadOnly) || target.readAll()!=bytes) throw std::runtime_error("Existing fixture sample differs; choose another output folder");
        } else if(!target.open(QIODevice::WriteOnly) || target.write(bytes)!=bytes.size()) throw std::runtime_error("Cannot copy supplied sample");
        if(set=="metadata") for(bool big:{false,true}) {
            Fixture::MetadataOptions options;options.big=big;
            Fixture::writeMetadata(out.filePath(big?"metadata_big_endian.jt":"metadata.jt"),"parts/Bolt.jt",options);
        }
        else {
            for(auto item:{std::pair<const char*,unsigned>{"draw_smooth.jt",0x0a},{"draw_flat.jt",0x1a},{"draw_unlit.jt",0x02},{"draw_culled.jt",0x0b},{"draw_wire_hint.jt",0x0e},{"draw_single_lighting.jt",0x08}})
                Fixture::writeDrawStyle(out.filePath(item.first),"parts/Bolt.jt",item.second);
            Fixture::writeDrawStyle(out.filePath("draw_flat_big_endian.jt"),"parts/Bolt.jt",0x1a,true);
            Fixture::writeDrawStyle(out.filePath("draw_invalid_version.jt"),"parts/Bolt.jt",0x0a,false,2);
        }
        QFile readme(out.filePath("README.txt"));
        if(!readme.open(QIODevice::WriteOnly)) throw std::runtime_error("Cannot write fixture instructions");
        if(set=="metadata") readme.write("Both JT files should load one opaque Bolt (913 vertices, 904 triangles).\nThe root name is Metadata assembly. Keep parts/ alongside the files.\nInspect typed metadata using: jt_probe metadata.jt --properties\nExpected root records: name, Unicode PartNumber, signed Count=-7, Mass=12.5, two Repeated values.\nThe child has its own Repeated value; properties are not inherited.\nExpected diagnostics: one unsupported pair; two unrepresentable pairs (long text and NaN).\nThe metadata is retained in the imported Assimp scene; no property browser or MVF metadata persistence is added.\n");
        else readme.write("Keep parts/ alongside the fixtures. All successful files have one mesh and 904 triangles.\n- draw_smooth.jt: smooth shaded, 913 vertices.\n- draw_flat.jt and draw_flat_big_endian.jt: visibly faceted normals, 2712 vertices.\n- draw_unlit.jt: flat material color without scene-light shading, 913 vertices.\n- draw_culled.jt: back-face culling enabled, 913 vertices. A closed bolt can look unchanged from outside.\n- draw_wire_hint.jt: filled viewport rendering, with an explicit wireframe limitation diagnostic; 913 vertices.\n- draw_single_lighting.jt: two-sided-lighting approximation diagnostic; 913 vertices.\n- draw_invalid_version.jt: unsupported draw style version error.\nInspect resolved materials using: jt_probe draw_flat.jt --materials\nUse a lit viewport mode for the smooth/flat/unlit comparison; global shading overrides can mask the source style.\n");
        std::cout<<out.absolutePath().toStdString()<<'\n';return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
