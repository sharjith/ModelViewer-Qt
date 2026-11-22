// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
//
// Stage X2 of the Parasolid X_T importer plan: a read-only view over a decoded Xt::File with
// field access by name, plus topology validation and per-file statistics. No geometry, no
// OCCT, no Qt. See research/parasolid/PARASOLID_IMPLEMENTATION_PLAN.md.
#pragma once

#include "XtReader.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Xt {

// Resolved position of one field of one node type; resolve once, read many times.
struct Slot {
    int type = -1;
    size_t offset = 0;  // into the node's values
    size_t width = 0;
    bool ok() const { return type >= 0; }
};

class Model {
public:
    // `file` must have been read with Options::keepValues = true and must outlive the model.
    explicit Model(const File& file);

    const File& file() const { return m_file; }
    const Node* find(int64_t index) const;  // nullptr when no node has that index
    const std::string& typeName(int type) const;

    // Slot of `field` in the node type called `nodeName` (e.g. "FIN", "loop").
    Slot slot(std::string_view nodeName, std::string_view field) const;
    // Value of element `element` of the slot; NaN when the slot or element is absent.
    double value(const Node& n, const Slot& s, size_t element = 0) const;
    // Pointer value: the node index, 0 when null, unset or absent.
    int64_t pointer(const Node& n, const Slot& s) const;

    const std::vector<const Node*>& nodesNamed(const std::string& name) const;
    size_t duplicateIndexes() const { return m_duplicates; }

private:
    const File& m_file;
    std::unordered_map<int64_t, size_t> m_byIndex;
    std::map<std::string, std::vector<const Node*>> m_byName;
    std::map<std::string, int> m_typeByName;
    std::map<int, std::vector<size_t>> m_fieldOffsets; // per type, per field
    std::map<int, std::vector<size_t>> m_fieldWidths;
    std::vector<const Node*> m_empty;
    std::string m_unknownName;
    size_t m_duplicates = 0;
};

struct TopologyReport {
    size_t bodies = 0, regions = 0, shells = 0, faces = 0, loops = 0, fins = 0, edges = 0, vertices = 0;
    size_t duplicateIndexes = 0;

    // Pointers into topology that name no node, or a node of the wrong kind ("FIN.loop" -> count).
    std::map<std::string, size_t> danglingByField;
    std::map<std::string, size_t> wrongTypeByField;

    // Structure: all counts are numbers of offending nodes.
    size_t shellFaceChainErrors = 0;     // face chain broken, or face.shell != shell
    size_t faceLoopChainErrors = 0;      // loop chain broken, or loop.face != face
    size_t loopFinRingErrors = 0;        // forward/backward ring not closed or inconsistent
    size_t finLoopMismatch = 0;          // fin.loop != the loop whose ring contains it
    size_t edgeFinRingErrors = 0;        // `other` ring not closed, or fin.edge != edge
    size_t loopsWithoutFins = 0;         // (vertex loops) informational
    size_t vertexFinMismatch = 0;        // vertex.fin whose fin.vertex is another vertex

    // Nodes not reached by walking from their parent.
    size_t unreachedFaces = 0, unreachedLoops = 0, unreachedFins = 0;
    size_t wireFins = 0;                  // fins with no loop (wire edges); informational

    std::map<size_t, size_t> finsPerEdge;     // ring length -> number of edges
    std::map<int64_t, size_t> eulerPerShell;  // V - E + F -> number of shells

    std::vector<std::string> samples;         // first few problems, with node indexes

    bool consistent() const;
};

TopologyReport validateTopology(const Model& model);

} // namespace Xt
