// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#include "XtLayout.h"

#include <cstdlib>
#include <sstream>

namespace Xt {

bool LayoutTable::parse(std::string_view text, std::string* error) {
    m_layouts.clear();
    std::istringstream lines{std::string(text)};
    std::string line;
    int lineNo = 0;
    auto fail = [&](const std::string& why) {
        if (error)
            *error = "layout line " + std::to_string(lineNo) + ": " + why;
        m_layouts.clear();
        return false;
    };
    while (std::getline(lines, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream in(line);
        NodeLayout layout;
        if (!(in >> layout.type >> layout.name) || layout.type <= 1)
            return fail("expected '<type> <NAME>'");
        std::string item;
        while (in >> item) {
            FieldDef f;
            const size_t colon = item.find(':');
            if (colon == std::string::npos || colon + 1 >= item.size())
                return fail("field '" + item + "' lacks ':type'");
            f.name = item.substr(0, colon);
            f.type = item[colon + 1];
            if (std::string("dpfvclnuwibh").find(f.type) == std::string::npos)
                return fail("unknown field type in '" + item + "'");
            const size_t bracket = item.find('[', colon);
            if (bracket != std::string::npos) {
                if (item.back() != ']')
                    return fail("unterminated bracket in '" + item + "'");
                const std::string n = item.substr(bracket + 1, item.size() - bracket - 2);
                f.count = n.empty() ? 1 : std::atoi(n.c_str());
                if (f.count < 1)
                    return fail("bad array size in '" + item + "'");
            }
            layout.fields.push_back(f);
        }
        if (m_layouts.count(layout.type))
            return fail("duplicate type " + std::to_string(layout.type));
        m_layouts[layout.type] = std::move(layout);
    }
    return true;
}

const NodeLayout* LayoutTable::find(int nodeType) const {
    const auto it = m_layouts.find(nodeType);
    return it == m_layouts.end() ? nullptr : &it->second;
}

} // namespace Xt
