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
    const QCommandLineOption output("output","Output folder for generated metadata fixtures","folder");
    parser.addOption(samples);parser.addOption(output);parser.process(app);
    if(!parser.isSet(samples) || !parser.isSet(output)) parser.showHelp(2);
    try {
        const QDir out(parser.value(output));
        QFile source(QDir(parser.value(samples)).filePath("Bolt.jt"));
        if(!source.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot read locally supplied Bolt.jt");
        const auto bytes=source.readAll();
        if(!QDir().mkpath(out.filePath("parts"))) throw std::runtime_error("Cannot create fixture directory");
        QFile target(out.filePath("parts/Bolt.jt"));
        if(target.exists()) {
            if(!target.open(QIODevice::ReadOnly) || target.readAll()!=bytes) throw std::runtime_error("Existing fixture sample differs; choose another output folder");
        } else if(!target.open(QIODevice::WriteOnly) || target.write(bytes)!=bytes.size()) throw std::runtime_error("Cannot copy supplied sample");
        for(bool big:{false,true}) {
            Fixture::MetadataOptions options;options.big=big;
            Fixture::writeMetadata(out.filePath(big?"metadata_big_endian.jt":"metadata.jt"),"parts/Bolt.jt",options);
        }
        QFile readme(out.filePath("README.txt"));
        if(!readme.open(QIODevice::WriteOnly)) throw std::runtime_error("Cannot write fixture instructions");
        readme.write("Both JT files should load one opaque Bolt (913 vertices, 904 triangles).\nThe root name is Metadata assembly. Keep parts/ alongside the files.\nInspect typed metadata using: jt_probe metadata.jt --properties\nExpected root records: name, Unicode PartNumber, signed Count=-7, Mass=12.5, two Repeated values.\nThe child has its own Repeated value; properties are not inherited.\nExpected diagnostics: one unsupported pair; two unrepresentable pairs (long text and NaN).\nThe metadata is retained in the imported Assimp scene; no property browser or MVF metadata persistence is added.\n");
        std::cout<<out.absolutePath().toStdString()<<'\n';return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
