// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
//
// Owner inspection tool for the XT reader (gate G1). Prints one JSON object per file, in
// the same fields as tools/parasolid/xt_decode.py so the results can be compared.
//
//   parasolid_xt_decode <layouts.txt> <file-or-directory> > out/parasolid-research/decode-cpp.json
//       Text X_T files. Needs the untracked base-layout text file.
//   parasolid_xt_decode --topology <layouts.txt> <file-or-directory> > out/parasolid-research/topology-cpp.json
//       Stage X2: topology validation and statistics per file (decodes with values kept).
//   parasolid_xt_decode --jt-binary <raw-xt-element-file>
//       Neutral-binary XT from a JT XT B-Rep element (or just its XT data); uses the tracked
//       JT table. Decoding starts at the first 'P' 'S' 0 0 flag sequence.
#include "XtJtLayouts.h"
#include "XtModel.h"
#include "XtReader.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static std::string readAll(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static std::string escape(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 0x20 || c > 0x7e) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
        else out += static_cast<char>(c);
    }
    return out;
}

static void printResult(const Xt::File& f, size_t fileBytes, bool first) {
    std::ostringstream o;
    o << (first ? "" : ",\n") << "{\"file_bytes\": " << fileBytes
      << ", \"transport\": \"" << (f.header.transport == Xt::Transport::Text ? "text" : "neutral-binary") << "\""
      << ", \"internal_schema\": \"" << escape(f.header.internalSchema) << "\""
      << ", \"status\": \"" << (f.status == Xt::Status::Complete ? "complete" : "stopped") << "\""
      << ", \"records\": " << f.nodes.size()
      << ", \"trailing_bytes\": " << f.trailingBytes
      << ", \"unresolved_pointers\": " << f.unresolvedPointers;
    if (!f.diagnostics.empty()) {
        const auto& d = f.diagnostics.front();
        o << ", \"error_code\": \"" << escape(d.code) << "\", \"error\": \"" << escape(d.message)
          << "\", \"node_type\": " << d.nodeType;
    }
    o << ", \"counts\": {";
    bool firstCount = true;
    for (const auto& kv : f.countsByName) {
        o << (firstCount ? "" : ", ") << "\"" << escape(kv.first) << "\": " << kv.second;
        firstCount = false;
    }
    o << "}}";
    std::fputs(o.str().c_str(), stdout);
}

static void printTopology(const Xt::File& f, size_t fileBytes, bool first) {
    std::ostringstream o;
    o << (first ? "" : ",\n") << "{\"file_bytes\": " << fileBytes << ", \"status\": \""
      << (f.status == Xt::Status::Complete ? "complete" : "stopped") << "\"";
    if (f.status == Xt::Status::Complete) {
        const Xt::Model model(f);
        const Xt::TopologyReport r = Xt::validateTopology(model);
        o << ", \"consistent\": " << (r.consistent() ? "true" : "false")
          << ", \"bodies\": " << r.bodies << ", \"regions\": " << r.regions << ", \"shells\": " << r.shells
          << ", \"faces\": " << r.faces << ", \"loops\": " << r.loops << ", \"fins\": " << r.fins
          << ", \"edges\": " << r.edges << ", \"vertices\": " << r.vertices
          << ", \"duplicate_indexes\": " << r.duplicateIndexes
          << ", \"shell_face_chain_errors\": " << r.shellFaceChainErrors
          << ", \"face_loop_chain_errors\": " << r.faceLoopChainErrors
          << ", \"loop_fin_ring_errors\": " << r.loopFinRingErrors
          << ", \"fin_loop_mismatch\": " << r.finLoopMismatch
          << ", \"edge_fin_ring_errors\": " << r.edgeFinRingErrors
          << ", \"loops_without_fins\": " << r.loopsWithoutFins
          << ", \"vertex_fin_mismatch\": " << r.vertexFinMismatch
          << ", \"unreached_faces\": " << r.unreachedFaces << ", \"unreached_loops\": " << r.unreachedLoops
          << ", \"unreached_fins\": " << r.unreachedFins << ", \"wire_fins\": " << r.wireFins;
        auto dump = [&](const char* key, const auto& m) {
            o << ", \"" << key << "\": {";
            bool firstItem = true;
            for (const auto& kv : m) {
                o << (firstItem ? "" : ", ") << "\"" << escape(std::to_string(kv.first)) << "\": " << kv.second;
                firstItem = false;
            }
            o << "}";
        };
        auto dumpNamed = [&](const char* key, const std::map<std::string, size_t>& m) {
            o << ", \"" << key << "\": {";
            bool firstItem = true;
            for (const auto& kv : m) {
                o << (firstItem ? "" : ", ") << "\"" << escape(kv.first) << "\": " << kv.second;
                firstItem = false;
            }
            o << "}";
        };
        dump("fins_per_edge", r.finsPerEdge);
        dump("euler_per_shell", r.eulerPerShell);
        dumpNamed("dangling", r.danglingByField);
        dumpNamed("wrong_type", r.wrongTypeByField);
        o << ", \"samples\": [";
        for (size_t i = 0; i < r.samples.size(); ++i)
            o << (i ? ", " : "") << "\"" << escape(r.samples[i]) << "\"";
        o << "]";
    } else if (!f.diagnostics.empty()) {
        o << ", \"error_code\": \"" << escape(f.diagnostics.front().code) << "\"";
    }
    o << "}";
    std::fputs(o.str().c_str(), stdout);
}

int main(int argc, char** argv) {
    bool topology = false;
    if (argc == 4 && std::string(argv[1]) == "--topology") {
        topology = true;
        argv[1] = argv[0];
        ++argv;
        --argc;
    }
    if (argc == 3 && std::string(argv[1]) == "--jt-binary") {
        const std::string bytes = readAll(argv[2]);
        const size_t at = bytes.find(std::string("PS\0\0", 4));
        if (at == std::string::npos) {
            std::fprintf(stderr, "no neutral-binary flag sequence found\n");
            return 1;
        }
        Xt::Options options;
        options.keepValues = false;
        std::puts("[");
        printResult(Xt::readNeutralBinary(std::string_view(bytes).substr(at), Xt::jtEmbeddedBaseLayouts(), options),
                    bytes.size(), true);
        std::puts("\n]");
        return 0;
    }
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <layouts.txt> <file-or-directory>\n       %s --jt-binary <raw-xt-element-file>\n",
                     argv[0], argv[0]);
        return 2;
    }
    Xt::LayoutTable table;
    std::string error;
    if (!table.parse(readAll(argv[1]), &error)) {
        std::fprintf(stderr, "layouts: %s\n", error.c_str());
        return 2;
    }
    std::vector<fs::path> files;
    const fs::path root = argv[2];
    if (fs::is_directory(root)) {
        for (const auto& e : fs::recursive_directory_iterator(root)) {
            std::string ext = e.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
            if (e.is_regular_file() && (ext == ".x_t" || ext == ".xmt_txt"))
                files.push_back(e.path());
        }
    } else {
        files.push_back(root);
    }
    std::sort(files.begin(), files.end());

    std::puts("[");
    bool first = true;
    for (const auto& path : files) {
        const std::string bytes = readAll(path);
        Xt::Options options;
        options.keepValues = topology; // counts only unless topology is requested
        const Xt::File decoded = Xt::readText(bytes, table, options);
        if (topology)
            printTopology(decoded, bytes.size(), first);
        else
            printResult(decoded, bytes.size(), first);
        first = false;
    }
    std::puts("\n]");
    return 0;
}
