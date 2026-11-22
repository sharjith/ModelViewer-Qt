// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
//
// Field layouts for XT node types. The base layouts are NOT part of the repository: they
// are loaded from a local text file (see research/parasolid/PARASOLID_PROVENANCE.md).
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Xt {

// type: d int, p pointer, f double, v vector (3 doubles), c char, l logical, n short,
//       u byte, w wide char, i interval (2), b box (6), h hvec (3 doubles).
// count: 0 scalar, 1 variable-length last field, n > 1 fixed array.
struct FieldDef {
    std::string name;
    char type = 'd';
    int count = 0;
};

struct NodeLayout {
    int type = 0;
    std::string name;
    std::vector<FieldDef> fields;
};

class LayoutTable {
public:
    // Text format, one type per line: "<type> <NAME> field:t field:t[] field:t[n] ...".
    // Lines starting with '#' and blank lines are ignored. Returns false and fills
    // `error` on malformed input.
    bool parse(std::string_view text, std::string* error);

    // Parasolid schema text file (sch_<n>.sch_txt): header block, then per node type
    // "<type> <NAME>; <description>; <a> <b> <variable>" followed by field lines
    // "<name>; <type>; <transmitted> <class> <count>". Fields marked as not transmitted are left out. Newer schema
    // files use other names for some nodes and fields; they are mapped to the names the builder reads. The schema
    // files are Siemens data and are never part of the repository.
    bool parseSchemaText(std::string_view text, std::string* error);

    // Newer schemas rename some nodes and fields (HALFEDGE for FIN, pt for pvec ...). Where a node of this table has the
    // same shape as the node of `reference` with the same number, but other names, the names of `reference` are taken, so
    // the builder can keep reading one set of names. Positions, types and counts are never changed.
    void adoptNamesFrom(const LayoutTable& reference);

    // For schema 13006 read from a schema file: drop what files with an embedded schema define themselves, so the table
    // can serve as the base of their edit scripts.
    void useAsEmbeddedBase();

    const NodeLayout* find(int nodeType) const;
    size_t size() const { return m_layouts.size(); }

private:
    std::map<int, NodeLayout> m_layouts;
};

// Table of schema <number> read from <dir>/sch_<number>.sch_txt (any letter case), cached per process; null when the
// file is missing or cannot be read. `error` (optional) says why.
const LayoutTable* schemaFromDirectory(const std::string& dir, int number, std::string* error = nullptr,
                                       const LayoutTable* namesFrom = nullptr);

} // namespace Xt
