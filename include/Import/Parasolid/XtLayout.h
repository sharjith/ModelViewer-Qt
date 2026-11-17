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

    const NodeLayout* find(int nodeType) const;
    size_t size() const { return m_layouts.size(); }

private:
    std::map<int, NodeLayout> m_layouts;
};

} // namespace Xt
