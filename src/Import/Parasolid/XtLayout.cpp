// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#include "XtLayout.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
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

bool LayoutTable::parseSchemaText(std::string_view text, std::string* error) {
    static const std::map<std::string, std::string> nodeNames = {
        {"HALFEDGE", "FIN"}, {"FEATURE", "GROUP"}, {"MEMBER_OF_FEATURE", "MEMBER_OF_GROUP"}};
    static const std::map<std::string, std::string> fieldNames = {
        {"attributes_features", "attributes_groups"}, {"halfedge", "fin"}, {"pt", "pvec"}, {"owning_feature", "owning_group"},
        {"original_u_start", "ou0"}, {"original_u_end", "ou1"}, {"original_v_start", "ov0"}, {"original_v_end", "ov1"},
        {"extended_u_start", "eu0"}, {"extended_u_end", "eu1"}, {"extended_v_start", "ev0"}, {"extended_v_end", "ev1"}};
    std::istringstream lines{std::string(text)};
    std::string line;
    bool inBody = false;
    std::string out;
    struct Pending {
        int type = 0;
        std::string name;
        bool variable = false;
        std::vector<std::pair<std::string, std::string>> fields; // "name:t" and its suffix is added at the end
        std::vector<int> counts;
    } cur;
    bool have = false;
    auto flush = [&] {
        if (!have)
            return;
        out += std::to_string(cur.type) + " " + cur.name;
        for (size_t i = 0; i < cur.fields.size(); ++i) {
            out += " " + cur.fields[i].first;
            const int count = cur.counts[i];
            if (count > 1)
                out += "[" + std::to_string(count) + "]";
            else if (count == 1 && cur.variable && i + 1 == cur.fields.size())
                out += "[]";
        }
        out += "\n";
        have = false;
    };
    auto trim = [](std::string s) {
        while (!s.empty() && (s.back() == '\r' || s.back() == ' '))
            s.pop_back();
        return s;
    };
    while (std::getline(lines, line)) {
        line = trim(line);
        if (!inBody) {
            if (line.compare(0, 15, "**END_OF_HEADER") == 0)
                inBody = true;
            continue;
        }
        // Node header: "<type> <NAME>; <description>; a b variable"
        const size_t semi1 = line.find(';');
        if (semi1 == std::string::npos)
            continue;
        const size_t semi2 = line.find(';', semi1 + 1);
        if (semi2 == std::string::npos)
            continue;
        const std::string first = line.substr(0, semi1);
        const std::string tail = line.substr(semi2 + 1);
        std::istringstream tailIn(tail);
        int a = 0, b = 0, c = 0;
        if (!(tailIn >> a >> b >> c))
            continue;
        const size_t space = first.find(' ');
        if (space != std::string::npos && first.find_first_not_of("0123456789") == space) {
            flush();
            cur = Pending{};
            cur.type = std::atoi(first.c_str());
            cur.name = first.substr(space + 1);
            const auto alias = nodeNames.find(cur.name);
            if (alias != nodeNames.end())
                cur.name = alias->second;
            cur.variable = c == 1;
            have = cur.type > 1 && a == 1; // a node type with a 0 here is never written to transmit files
            continue;
        }
        // Field line: "<name>; <type>; <transmitted> <class> <count>"
        if (!have || first.find(' ') != std::string::npos)
            continue;
        std::string typeText = trim(line.substr(semi1 + 1, semi2 - semi1 - 1));
        while (!typeText.empty() && typeText.front() == ' ')
            typeText.erase(typeText.begin());
        if (typeText.size() != 1)
            continue;
        if (a != 1)
            continue; // not transmitted
        std::string name = first;
        if (const auto alias = fieldNames.find(name); alias != fieldNames.end())
            name = alias->second;
        char type = typeText[0];
        if (type == 't')
            type = 'd'; // tag
        cur.fields.emplace_back(name + ":" + type, "");
        cur.counts.push_back(c);
    }
    flush();
    if (out.empty()) {
        if (error)
            *error = "no node types found in the schema text";
        return false;
    }
    return parse(out, error);
}

const LayoutTable* schemaFromDirectory(const std::string& dir, int number, std::string* error, const LayoutTable* namesFrom) {
    static std::mutex mutex;
    static std::map<std::pair<std::string, int>, std::unique_ptr<LayoutTable>> cache;
    std::lock_guard<std::mutex> lock(mutex);
    const auto key = std::make_pair(dir, number);
    if (const auto it = cache.find(key); it != cache.end())
        return it->second.get();
    namespace fs = std::filesystem;
    std::string wanted = "sch_" + std::to_string(number) + ".sch_txt";
    std::error_code ec;
    fs::path found;
    for (const auto& entry : fs::directory_iterator(fs::u8path(dir), ec)) {
        std::string name = entry.path().filename().string();
        for (char& c : name)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (name == wanted) {
            found = entry.path();
            break;
        }
    }
    std::unique_ptr<LayoutTable> table;
    if (found.empty()) {
        if (error)
            *error = wanted + " not found in " + dir;
    } else {
        std::ifstream in(found, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        table = std::make_unique<LayoutTable>();
        if (!table->parseSchemaText(text, error))
            table.reset();
        else if (namesFrom)
            table->adoptNamesFrom(*namesFrom);
        if (table && number == 13006)
            table->useAsEmbeddedBase();
    }
    if (!table)
        return nullptr; // not cached: the file may be added later
    return cache.emplace(key, std::move(table)).first->second.get();
}

void LayoutTable::useAsEmbeddedBase() {
    // The edit scripts of embedded schemas are written against the 13006 layout of a LIST header without the three fields
    // the later schema text lists last-but-not-least; files with an embedded schema add them back themselves.
    const auto it = m_layouts.find(70);
    if (it == m_layouts.end())
        return;
    auto& fields = it->second.fields;
    fields.erase(std::remove_if(fields.begin(), fields.end(), [](const FieldDef& f) {
                     return f.name == "finger_block" || f.name == "finger_index" || f.name == "notransmit";
                 }),
                 fields.end());
}

void LayoutTable::adoptNamesFrom(const LayoutTable& reference) {
    for (auto& [type, layout] : m_layouts) {
        const NodeLayout* ref = reference.find(type);
        if (!ref)
            continue;
        if (layout.name != ref->name && ref->fields.size() >= 1)
            layout.name = ref->name;
        const size_t n = std::min(layout.fields.size(), ref->fields.size());
        for (size_t i = 0; i < n; ++i) {
            FieldDef& f = layout.fields[i];
            const FieldDef& r = ref->fields[i];
            if (f.name == r.name || f.type != r.type || f.count != r.count)
                continue;
            const bool refHasName = std::any_of(ref->fields.begin(), ref->fields.end(), [&](const FieldDef& x) { return x.name == f.name; });
            const bool selfHasName = std::any_of(layout.fields.begin(), layout.fields.end(), [&](const FieldDef& x) { return x.name == r.name; });
            if (!refHasName && !selfHasName)
                f.name = r.name;
        }
    }
}

const NodeLayout* LayoutTable::find(int nodeType) const {
    const auto it = m_layouts.find(nodeType);
    return it == m_layouts.end() ? nullptr : &it->second;
}

} // namespace Xt
