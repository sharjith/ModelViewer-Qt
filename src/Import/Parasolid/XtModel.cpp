// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#include "XtModel.h"

#include <cmath>
#include <unordered_set>

namespace Xt {

namespace {

size_t scalarWidth(char type) {
    switch (type) {
    case 'v': case 'h': return 3;
    case 'i': return 2;
    case 'b': return 6;
    default: return 1;
    }
}

} // namespace

Model::Model(const File& file) : m_file(file) {
    for (const auto& kv : file.layouts) {
        m_typeByName[kv.second.name] = kv.first;
        size_t off = 0;
        auto& offsets = m_fieldOffsets[kv.first];
        auto& widths = m_fieldWidths[kv.first];
        for (const FieldDef& f : kv.second.fields) {
            offsets.push_back(off);
            const size_t w = scalarWidth(f.type) * (f.count > 1 ? static_cast<size_t>(f.count) : 1);
            widths.push_back(f.count == 1 ? 0 : w); // variable-length last field: width per node
            off += w;
        }
    }
    m_byIndex.reserve(file.nodes.size());
    for (size_t i = 0; i < file.nodes.size(); ++i) {
        const Node& n = file.nodes[i];
        if (!m_byIndex.emplace(n.index, i).second)
            ++m_duplicates;
        const auto it = file.layouts.find(n.type);
        if (it != file.layouts.end())
            m_byName[it->second.name].push_back(&n);
    }
}

const Node* Model::find(int64_t index) const {
    const auto it = m_byIndex.find(index);
    return it == m_byIndex.end() ? nullptr : &m_file.nodes[it->second];
}

const std::string& Model::typeName(int type) const {
    const auto it = m_file.layouts.find(type);
    return it == m_file.layouts.end() ? m_unknownName : it->second.name;
}

Slot Model::slot(std::string_view nodeName, std::string_view field) const {
    const auto t = m_typeByName.find(std::string(nodeName));
    if (t == m_typeByName.end())
        return {};
    const NodeLayout& layout = m_file.layouts.at(t->second);
    for (size_t i = 0; i < layout.fields.size(); ++i) {
        if (layout.fields[i].name == field) {
            Slot s;
            s.type = t->second;
            s.offset = m_fieldOffsets.at(t->second)[i];
            s.width = m_fieldWidths.at(t->second)[i];
            return s;
        }
    }
    return {};
}

double Model::value(const Node& n, const Slot& s, size_t element) const {
    if (!s.ok() || n.type != s.type)
        return std::nan("");
    const size_t at = s.offset + element;
    if (at >= n.valueCount)
        return std::nan("");
    return m_file.values[n.valueOffset + at];
}

int64_t Model::pointer(const Node& n, const Slot& s) const {
    const double v = value(n, s);
    if (std::isnan(v) || v <= 0)
        return 0;
    return static_cast<int64_t>(v);
}

const std::vector<const Node*>& Model::nodesNamed(const std::string& name) const {
    const auto it = m_byName.find(name);
    return it == m_byName.end() ? m_empty : it->second;
}

bool TopologyReport::consistent() const {
    return danglingByField.empty() && wrongTypeByField.empty() && shellFaceChainErrors == 0 &&
           faceLoopChainErrors == 0 && loopFinRingErrors == 0 && finLoopMismatch == 0 &&
           edgeFinRingErrors == 0 && unreachedFaces == 0 && unreachedLoops == 0 &&
           unreachedFins == 0 && duplicateIndexes == 0;
}

TopologyReport validateTopology(const Model& m) {
    TopologyReport r;
    const File& file = m.file();
    const Node* base = file.nodes.empty() ? nullptr : file.nodes.data();
    auto ordinal = [&](const Node* n) { return static_cast<size_t>(n - base); };
    auto note = [&](const std::string& text) {
        if (r.samples.size() < 20)
            r.samples.push_back(text);
    };

    const auto& bodies = m.nodesNamed("BODY");
    const auto& regions = m.nodesNamed("REGION");
    const auto& shells = m.nodesNamed("SHELL");
    const auto& faces = m.nodesNamed("FACE");
    const auto& loops = m.nodesNamed("LOOP");
    const auto& fins = m.nodesNamed("FIN");
    const auto& edges = m.nodesNamed("EDGE");
    const auto& vertices = m.nodesNamed("VERTEX");
    r.bodies = bodies.size(); r.regions = regions.size(); r.shells = shells.size();
    r.faces = faces.size(); r.loops = loops.size(); r.fins = fins.size();
    r.edges = edges.size(); r.vertices = vertices.size();
    r.duplicateIndexes = m.duplicateIndexes();

    const Slot shellFace = m.slot("SHELL", "face"), faceNext = m.slot("FACE", "next"),
               faceShell = m.slot("FACE", "shell"), faceLoop = m.slot("FACE", "loop"),
               loopNext = m.slot("LOOP", "next"), loopFace = m.slot("LOOP", "face"),
               loopFin = m.slot("LOOP", "fin"), finLoop = m.slot("FIN", "loop"),
               finFwd = m.slot("FIN", "forward"), finBack = m.slot("FIN", "backward"),
               finOther = m.slot("FIN", "other"), finEdge = m.slot("FIN", "edge"),
               finVertex = m.slot("FIN", "vertex"), edgeFin = m.slot("EDGE", "fin"),
               vertexFin = m.slot("VERTEX", "fin"), bodyShell = m.slot("BODY", "shell");

    // Resolve a pointer, counting dangling and wrong-kind references.
    auto resolve = [&](const Node& from, const Slot& s, const char* label, const char* expectName, bool report = true) -> const Node* {
        if (!report) {
            const int64_t q = m.pointer(from, s);
            const Node* t = q ? m.find(q) : nullptr;
            return t && (!expectName || m.typeName(t->type) == expectName) ? t : nullptr;
        }
        const int64_t p = m.pointer(from, s);
        if (p == 0)
            return nullptr;
        const Node* to = m.find(p);
        if (!to) {
            ++r.danglingByField[label];
            note(std::string(label) + " at node " + std::to_string(from.index) + " -> missing " + std::to_string(p));
            return nullptr;
        }
        if (expectName && m.typeName(to->type) != expectName) {
            ++r.wrongTypeByField[label];
            note(std::string(label) + " at node " + std::to_string(from.index) + " -> " + m.typeName(to->type));
            return nullptr;
        }
        return to;
    };

    std::vector<char> faceSeen(file.nodes.size(), 0), loopSeen(file.nodes.size(), 0), finSeen(file.nodes.size(), 0);
    const size_t bound = file.nodes.size() + 2;

    // Body -> shell references (informational dangling check only).
    for (const Node* b : bodies)
        resolve(*b, bodyShell, "BODY.shell", "SHELL");

    for (const Node* sh : shells) {
        std::unordered_set<int64_t> shellVertices, shellEdges;
        size_t shellFaces = 0;
        const Node* face = resolve(*sh, shellFace, "SHELL.face", "FACE");
        for (size_t steps = 0; face && steps < bound; ++steps) {
            if (faceSeen[ordinal(face)]) {
                ++r.shellFaceChainErrors;
                note("face chain revisits node " + std::to_string(face->index));
                break;
            }
            faceSeen[ordinal(face)] = 1;
            ++shellFaces;
            const Node* owner = resolve(*face, faceShell, "FACE.shell", "SHELL");
            if (owner != sh) {
                ++r.shellFaceChainErrors;
                note("face " + std::to_string(face->index) + " has another shell");
            }
            // Loops of this face.
            const Node* loop = resolve(*face, faceLoop, "FACE.loop", "LOOP");
            for (size_t ls = 0; loop && ls < bound; ++ls) {
                if (loopSeen[ordinal(loop)]) {
                    ++r.faceLoopChainErrors;
                    note("loop chain revisits node " + std::to_string(loop->index));
                    break;
                }
                loopSeen[ordinal(loop)] = 1;
                if (resolve(*loop, loopFace, "LOOP.face", "FACE") != face) {
                    ++r.faceLoopChainErrors;
                    note("loop " + std::to_string(loop->index) + " has another face");
                }
                // Fin ring of this loop.
                const Node* start = resolve(*loop, loopFin, "LOOP.fin", "FIN");
                if (!start) {
                    ++r.loopsWithoutFins;
                } else {
                    const Node* fin = start;
                    bool closed = false;
                    for (size_t fs = 0; fs < bound; ++fs) {
                        if (finSeen[ordinal(fin)]) {
                            ++r.loopFinRingErrors;
                            note("fin ring revisits node " + std::to_string(fin->index));
                            break;
                        }
                        finSeen[ordinal(fin)] = 1;
                        if (resolve(*fin, finLoop, "FIN.loop", "LOOP") != loop) {
                            ++r.finLoopMismatch;
                            note("fin " + std::to_string(fin->index) + " belongs to another loop");
                        }
                        const Node* edge = resolve(*fin, finEdge, "FIN.edge", "EDGE");
                        if (edge)
                            shellEdges.insert(edge->index);
                        const Node* vertex = resolve(*fin, finVertex, "FIN.vertex", "VERTEX");
                        if (vertex)
                            shellVertices.insert(vertex->index);
                        const Node* next = resolve(*fin, finFwd, "FIN.forward", "FIN");
                        const Node* back = next ? resolve(*next, finBack, "FIN.backward", "FIN", false) : nullptr;
                        if (!next || back != fin) {
                            ++r.loopFinRingErrors;
                            note("fin " + std::to_string(fin->index) + " forward/backward inconsistent");
                            break;
                        }
                        fin = next;
                        if (fin == start) {
                            closed = true;
                            break;
                        }
                    }
                    if (!closed)
                        ++r.loopFinRingErrors;
                }
                loop = resolve(*loop, loopNext, "LOOP.next", "LOOP");
            }
            face = resolve(*face, faceNext, "FACE.next", "FACE");
        }
        ++r.eulerPerShell[static_cast<int64_t>(shellVertices.size()) - static_cast<int64_t>(shellEdges.size()) +
                          static_cast<int64_t>(shellFaces)];
    }

    for (const Node* f : faces)
        if (!faceSeen[ordinal(f)])
            ++r.unreachedFaces;
    for (const Node* l : loops)
        if (!loopSeen[ordinal(l)])
            ++r.unreachedLoops;
    for (const Node* f : fins) {
        if (finSeen[ordinal(f)])
            continue;
        if (m.pointer(*f, finLoop) == 0)
            ++r.wireFins; // a fin with no loop belongs to a wire or acorn edge, not to a face
        else
            ++r.unreachedFins;
    }

    // Edge -> fin rings around the edge, linked through `other`.
    for (const Node* e : edges) {
        const Node* start = resolve(*e, edgeFin, "EDGE.fin", "FIN");
        if (!start)
            continue;
        size_t length = 0;
        const Node* fin = start;
        bool ok = true;
        for (size_t steps = 0; fin && steps < bound; ++steps) {
            ++length;
            if (resolve(*fin, finEdge, "FIN.edge", "EDGE", false) != e) {
                ok = false;
                note("fin " + std::to_string(fin->index) + " on the ring of edge " + std::to_string(e->index) + " names another edge");
                break;
            }
            fin = resolve(*fin, finOther, "FIN.other", "FIN");
            if (fin == start)
                break;
            if (!fin)
                break; // ring ends (a single-sided edge)
        }
        if (!ok)
            ++r.edgeFinRingErrors;
        ++r.finsPerEdge[length];
    }

    for (const Node* v : vertices) {
        const Node* fin = resolve(*v, vertexFin, "VERTEX.fin", "FIN");
        if (fin) {
            const Node* back = resolve(*fin, finVertex, "FIN.vertex", "VERTEX", false);
            if (back && back != v)
                ++r.vertexFinMismatch;
        }
    }
    return r;
}

} // namespace Xt
