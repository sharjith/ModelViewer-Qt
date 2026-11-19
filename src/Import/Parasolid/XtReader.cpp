// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#include "XtReader.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <tuple>
#include <unordered_set>

namespace Xt {
namespace {

struct Fail {
    std::string code;
    std::string message;
    int64_t nodeType;
};

const double kUnset = std::numeric_limits<double>::quiet_NaN();
constexpr int64_t kBinaryUnsetInt = -32764;
constexpr double kBinaryUnsetReal = -3.14158e13;

bool validFieldType(char t) { return std::string_view("dpfvclnuwibh").find(t) != std::string_view::npos; }

// ---------------------------------------------------------------------------------
// Text transport
// ---------------------------------------------------------------------------------

// Text stream with the physical layout removed: newlines, carriage returns and trailing
// spaces at the end of a physical record carry no meaning.
std::string normalise(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t end = raw.find('\n', pos);
        if (end == std::string_view::npos)
            end = raw.size();
        size_t last = end;
        if (last > pos && raw[last - 1] == '\r')
            --last;
        while (last > pos && raw[last - 1] == ' ')
            --last;
        out.append(raw.substr(pos, last - pos));
        pos = end + 1;
    }
    return out;
}

// Both transports expose the same operations, so the record decoder is written once.
class TextSource {
public:
    TextSource(const std::string& buf, const Limits& limits, size_t pos) : m_buf(buf), m_limits(limits), m_pos(pos) {}

    static constexpr bool kAllowTrailing = false;

    bool atEnd() const { return m_pos >= m_buf.size(); }
    size_t remaining() const { return m_pos >= m_buf.size() ? 0 : m_buf.size() - m_pos; }

    int64_t nodeType() { return integer(); }
    int64_t terminatorIndex() { return integer(); }
    int64_t nodeIndex() { return integer(); }
    int64_t variableCount() { return integer(); }
    int64_t fieldCount() { return integer(); }
    int64_t ptrClass() { return integer(); }
    int64_t eltCount() { return integer(); }
    std::string shortString() { return string(); }
    std::string longString() { return string(); }
    int64_t headerInt() { return integer(); }

    bool consumeBaseFlag() {
        if (std::string_view(m_buf).substr(m_pos, 4) != "255 ")
            return false;
        m_pos += 4;
        return true;
    }
    bool nextIsEditOp(bool /*baseKnown*/) const {
        const char c = peek();
        return c == 'C' || c == 'D' || c == 'I' || c == 'A' || c == 'Z';
    }
    char editOp() { return character(); }
    void transmitFlag() { character(); }

    void element(char type, std::vector<double>* out) {
        auto push = [&](double v) { if (out) out->push_back(v); };
        switch (type) {
        case 'd': case 'p': case 'n': case 'u': case 'w': {
            const std::string_view t = token();
            if (t == "?") { push(kUnset); break; }
            push(static_cast<double>(parseInt(t)));
            break;
        }
        case 'f': push(real()); break;
        case 'c': case 'l': push(static_cast<unsigned char>(character())); break;
        case 'v': {
            const double x = real();
            if (std::isnan(x)) { push(kUnset); push(kUnset); push(kUnset); break; } // whole vector unset
            push(x); push(real()); push(real());
            break;
        }
        case 'h': for (int i = 0; i < 3; ++i) push(real()); break;
        case 'i': for (int i = 0; i < 2; ++i) push(real()); break;
        case 'b': for (int i = 0; i < 6; ++i) push(real()); break;
        default: throw Fail{"bad-schema", "unknown field type", -1};
        }
    }

private:
    char peek() const { return m_pos < m_buf.size() ? m_buf[m_pos] : '\0'; }

    // One space-terminated token. An unset marker '?' is a complete token on its own and
    // is not followed by a space.
    std::string_view token() {
        if (m_pos >= m_buf.size())
            throw Fail{"truncated", "unexpected end of stream", -1};
        if (m_buf[m_pos] == '?') {
            ++m_pos;
            return "?";
        }
        size_t end = m_buf.find(' ', m_pos);
        if (end == std::string::npos)
            end = m_buf.size();
        const std::string_view t(m_buf.data() + m_pos, end - m_pos);
        // A final token with no trailing space must not push the cursor past the end.
        m_pos = std::min(end + 1, m_buf.size());
        return t;
    }

    static int64_t parseInt(std::string_view t) {
        int64_t v = 0;
        const auto r = std::from_chars(t.data(), t.data() + t.size(), v);
        if (t.empty() || r.ec != std::errc() || r.ptr != t.data() + t.size())
            throw Fail{"bad-number", "expected an integer, found '" + std::string(t.substr(0, 24)) + "'", -1};
        return v;
    }

    int64_t integer() { return parseInt(token()); }

    double real() {
        const std::string_view t = token();
        if (t == "?")
            return kUnset;
        double v = 0;
        const auto r = std::from_chars(t.data(), t.data() + t.size(), v);
        if (t.empty() || r.ec != std::errc() || r.ptr != t.data() + t.size())
            throw Fail{"bad-number", "expected a number, found '" + std::string(t.substr(0, 24)) + "'", -1};
        return v;
    }

    char character() {
        if (m_pos >= m_buf.size())
            throw Fail{"truncated", "unexpected end of stream", -1};
        return m_buf[m_pos++];
    }

    // Length-prefixed string: "<n> " followed by n characters, no terminator.
    std::string string() {
        const int64_t n = integer();
        if (n < 0 || n > m_limits.maxStringLength)
            throw Fail{"limit", "string length out of range", -1};
        if (m_pos + static_cast<size_t>(n) > m_buf.size())
            throw Fail{"truncated", "string runs past end of stream", -1};
        std::string s = m_buf.substr(m_pos, static_cast<size_t>(n));
        m_pos += static_cast<size_t>(n);
        return s;
    }

    const std::string& m_buf;
    const Limits& m_limits;
    size_t m_pos;
};

// ---------------------------------------------------------------------------------
// Neutral binary transport (big-endian, IEEE doubles, ASCII)
// ---------------------------------------------------------------------------------

class BinarySource {
public:
    BinarySource(std::string_view buf, const Limits& limits, size_t pos) : m_buf(buf), m_limits(limits), m_pos(pos) {}

    static constexpr bool kAllowTrailing = true; // JT carries integer attribute data after the XT stream

    bool atEnd() const { return m_pos >= m_buf.size(); }
    size_t remaining() const { return m_pos >= m_buf.size() ? 0 : m_buf.size() - m_pos; }

    int64_t nodeType() { return i16(); }
    int64_t terminatorIndex() { return pointerIndex(); }
    int64_t nodeIndex() { return pointerIndex(); }
    int64_t variableCount() { return i32(); }
    int64_t fieldCount() { return u8(); }
    int64_t ptrClass() { return i16(); }
    int64_t eltCount() { return pointerIndex(); }
    std::string shortString() { return bytes(u8()); }
    std::string longString(int64_t n) { return bytes(n); }
    std::string longString() { return bytes(i32()); }
    int64_t headerInt() { return i32(); }
    int64_t headerShort() { return i16(); }

    bool consumeBaseFlag() {
        if (m_pos < m_buf.size() && static_cast<unsigned char>(m_buf[m_pos]) == 0xFF) {
            ++m_pos;
            return true;
        }
        return false;
    }
    // In binary an edit operation and a string length are both single bytes, so decide by
    // knowledge: a type present in the base table is edited; an unknown type is read as a
    // full definition unless its first byte is unambiguously an operation letter.
    bool nextIsEditOp(bool baseKnown) const {
        if (baseKnown)
            return true;
        const unsigned char c = m_pos < m_buf.size() ? static_cast<unsigned char>(m_buf[m_pos]) : 0;
        return c == 'C' || c == 'D' || c == 'I' || c == 'A' || c == 'Z';
    }
    char editOp() { return static_cast<char>(u8()); }
    void transmitFlag() { u8(); }

    void element(char type, std::vector<double>* out) {
        auto push = [&](double v) { if (out) out->push_back(v); };
        auto real = [&]() { const double v = f64(); return v == kBinaryUnsetReal ? kUnset : v; };
        switch (type) {
        case 'd': { const int64_t v = i32(); push(v == kBinaryUnsetInt ? kUnset : static_cast<double>(v)); break; }
        case 'p': { const int64_t v = pointerIndex(); push(static_cast<double>(v)); break; }
        case 'n': case 'w': { const int64_t v = i16(); push(v == kBinaryUnsetInt ? kUnset : static_cast<double>(v)); break; }
        case 'u': case 'c': case 'l': push(static_cast<double>(u8())); break;
        case 'f': push(real()); break;
        case 'v': case 'h': for (int i = 0; i < 3; ++i) push(real()); break;
        case 'i': for (int i = 0; i < 2; ++i) push(real()); break;
        case 'b': for (int i = 0; i < 6; ++i) push(real()); break;
        default: throw Fail{"bad-schema", "unknown field type", -1};
        }
    }

private:
    void need(size_t n) const {
        if (m_pos + n > m_buf.size())
            throw Fail{"truncated", "unexpected end of binary stream", -1};
    }
    unsigned u8() {
        need(1);
        return static_cast<unsigned char>(m_buf[m_pos++]);
    }
    int64_t i16() {
        need(2);
        const int v = static_cast<int16_t>((static_cast<unsigned char>(m_buf[m_pos]) << 8) | static_cast<unsigned char>(m_buf[m_pos + 1]));
        m_pos += 2;
        return v;
    }
    int64_t i32() {
        need(4);
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
            v = (v << 8) | static_cast<unsigned char>(m_buf[m_pos + i]);
        m_pos += 4;
        return static_cast<int32_t>(v);
    }
    double f64() {
        need(8);
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i)
            v = (v << 8) | static_cast<unsigned char>(m_buf[m_pos + i]);
        m_pos += 8;
        double d;
        std::memcpy(&d, &v, sizeof d);
        return d;
    }
    // Small indices are one 2-byte value (index + 1); larger ones are a negative
    // remainder followed by a quotient.
    int64_t pointerIndex() {
        int64_t r = i16();
        int64_t q = 0;
        if (r < 0) {
            q = i16();
            r = -r;
        }
        return q * 32767 + r - 1;
    }
    std::string bytes(int64_t n) {
        if (n < 0 || n > m_limits.maxStringLength)
            throw Fail{"limit", "string length out of range", -1};
        need(static_cast<size_t>(n));
        std::string s(m_buf.substr(m_pos, static_cast<size_t>(n)));
        m_pos += static_cast<size_t>(n);
        return s;
    }

    std::string_view m_buf;
    const Limits& m_limits;
    size_t m_pos;
};

// ---------------------------------------------------------------------------------
// Shared record decoding
// ---------------------------------------------------------------------------------

template <class Source>
FieldDef readFieldDefinition(Source& s) {
    FieldDef f;
    f.name = s.shortString();
    const int64_t ptrClass = s.ptrClass();
    const int64_t elts = s.eltCount();
    if (elts < 0 || elts > 1'000'000)
        throw Fail{"bad-schema", "field element count out of range", -1};
    f.count = static_cast<int>(elts);
    if (ptrClass != 0) {
        f.type = 'p';
    } else {
        const std::string type = s.shortString();
        if (type.size() != 1 || !validFieldType(type[0]))
            throw Fail{"bad-schema", "unknown field type '" + type + "'", -1};
        f.type = type[0];
        if (f.count == 1)
            s.transmitFlag(); // present only for variable-length fields
    }
    return f;
}

template <class Source>
void decodeRecords(File& file, Source& s, const LayoutTable& base, const Options& options, int64_t* lastType,
                   std::unordered_set<int64_t>* seen, std::vector<std::tuple<int64_t, int, int>>* pointers,
                   bool externalLayouts = false) {
    size_t polled = 0;
    for (;;) {
        if (++polled % 4096 == 0 && options.cancelled && options.cancelled())
            throw Fail{"cancelled", "cancelled by caller", -1};
        const int64_t type = s.nodeType();
        *lastType = type;
        if (type == 1) { // terminator
            s.terminatorIndex();
            if (!Source::kAllowTrailing && !s.atEnd())
                throw Fail{"trailing-data", "data follows the terminator", -1};
            file.trailingBytes = s.remaining();
            return;
        }
        if (type < 2 || type > 100000)
            throw Fail{"bad-node-type", "node type " + std::to_string(type) + " out of range", type};
        if (file.nodes.size() >= options.limits.maxRecords)
            throw Fail{"limit", "record limit reached", type};

        const int t = static_cast<int>(type);
        if (file.layouts.find(t) == file.layouts.end()) {
            const NodeLayout* baseLayout = base.find(t);
            if (externalLayouts) {
                // The schema is not in the file: every node type is laid out by the schema table.
                if (!baseLayout)
                    throw Fail{"unknown-base-layout", "node type " + std::to_string(t) + " is not in the schema", type};
                file.layouts[t] = *baseLayout;
            } else if (s.consumeBaseFlag()) {
                if (!baseLayout)
                    throw Fail{"unknown-base-layout", "node type " + std::to_string(t) + " uses the base layout, which is not known", type};
                file.layouts[t] = *baseLayout;
            } else {
                const int64_t fieldCount = s.fieldCount();
                if (s.nextIsEditOp(baseLayout != nullptr)) {
                    if (!baseLayout)
                        throw Fail{"unknown-base-layout", "node type " + std::to_string(t) + " is edited from a base layout that is not known", type};
                    NodeLayout layout;
                    layout.type = t;
                    layout.name = baseLayout->name;
                    size_t i = 0;
                    for (;;) {
                        const char op = s.editOp();
                        if (op == 'C') {
                            if (i >= baseLayout->fields.size())
                                throw Fail{"bad-edit-script", "copy past the end of the base layout of type " + std::to_string(t), type};
                            layout.fields.push_back(baseLayout->fields[i++]);
                        } else if (op == 'D') {
                            if (i >= baseLayout->fields.size())
                                throw Fail{"bad-edit-script", "delete past the end of the base layout of type " + std::to_string(t), type};
                            ++i;
                        } else if (op == 'I' || op == 'A') {
                            layout.fields.push_back(readFieldDefinition(s));
                        } else if (op == 'Z') {
                            break;
                        } else {
                            throw Fail{"bad-edit-script", std::string("unknown edit operation '") + op + "'", type};
                        }
                    }
                    if (i != baseLayout->fields.size())
                        throw Fail{"base-layout-mismatch", "edit script of type " + std::to_string(t) + " does not consume the base layout (" + std::to_string(i) + " of " + std::to_string(baseLayout->fields.size()) + " fields)", type};
                    if (static_cast<int64_t>(layout.fields.size()) != fieldCount)
                        throw Fail{"bad-edit-script", "edit script of type " + std::to_string(t) + " yields a different field count than declared", type};
                    file.layouts[t] = std::move(layout);
                } else {
                    // A node type that does not exist in the base schema: the file carries
                    // its full definition, so it can be read generically.
                    NodeLayout layout;
                    layout.type = t;
                    layout.name = s.shortString();
                    s.shortString(); // description
                    if (fieldCount < 0 || fieldCount > 1000)
                        throw Fail{"bad-schema", "field count out of range", type};
                    for (int64_t k = 0; k < fieldCount; ++k)
                        layout.fields.push_back(readFieldDefinition(s));
                    file.inStreamTypes[t] = layout.name;
                    file.layouts[t] = std::move(layout);
                }
            }
        }

        const NodeLayout& layout = file.layouts[t];
        const bool variable = !layout.fields.empty() && layout.fields.back().count == 1;
        int64_t variableCount = 0;
        if (variable) {
            variableCount = s.variableCount();
            if (variableCount < 0 || variableCount > options.limits.maxVariableCount)
                throw Fail{"limit", "variable-length count out of range", type};
        }
        Node node;
        node.type = t;
        node.index = s.nodeIndex();
        node.valueOffset = file.values.size();
        std::vector<double>* sink = options.keepValues ? &file.values : nullptr;
        std::vector<double> scratch;
        for (size_t fi = 0; fi < layout.fields.size(); ++fi) {
            const FieldDef& f = layout.fields[fi];
            const int64_t n = f.count == 0 ? 1 : (f.count == 1 ? variableCount : f.count);
            if (file.values.size() + static_cast<size_t>(n) > options.limits.maxValues)
                throw Fail{"limit", "decoded value limit reached", type};
            for (int64_t k = 0; k < n; ++k) {
                scratch.clear();
                s.element(f.type, &scratch);
                if (f.type == 'p' && f.count == 0 && !std::isnan(scratch[0]) && scratch[0] > 0)
                    pointers->emplace_back(static_cast<int64_t>(scratch[0]), t, static_cast<int>(fi));
                if (sink)
                    sink->insert(sink->end(), scratch.begin(), scratch.end());
            }
        }
        node.valueCount = file.values.size() - node.valueOffset;
        if (std::getenv("XT_DEBUG_DECODE") && file.nodes.size() < 12)
            std::fprintf(stderr, "decode record %zu: type %d %s index %lld values %zu variable %lld fields %zu" "%c", file.nodes.size(), t, layout.name.c_str(), static_cast<long long>(node.index), node.valueCount, static_cast<long long>(variableCount), layout.fields.size(), 10);
        seen->insert(node.index);
        ++file.countsByName[layout.name];
        file.nodes.push_back(node);
    }
}

void finishPointers(File& file, const std::unordered_set<int64_t>& seen, const std::vector<std::tuple<int64_t, int, int>>& pointers) {
    for (const auto& p : pointers) {
        if (seen.count(std::get<0>(p)))
            continue;
        ++file.unresolvedPointers;
        const auto it = file.layouts.find(std::get<1>(p));
        if (it != file.layouts.end())
            ++file.unresolvedByField[it->second.name + "." + it->second.fields[static_cast<size_t>(std::get<2>(p))].name];
    }
}

void recordFailure(File& file, const Fail& f, int64_t lastType) {
    file.diagnostics.push_back({f.code, f.message, f.nodeType >= 0 ? f.nodeType : lastType,
                                static_cast<int64_t>(file.nodes.size())});
}

void parseModellerVersion(Header& h) {
    h.partition = h.modellerString.find("(partition)") != std::string::npos;
    const size_t at = h.modellerString.rfind(' ');
    h.modellerVersion = at == std::string::npos ? 0 : std::atoll(h.modellerString.c_str() + at + 1);
}

void checkSchemaKey(Header& h) {
    if (h.internalSchema.compare(0, 4, "SCH_") != 0)
        throw Fail{"bad-schema", "internal schema key not recognised", -1};
    size_t underscores = 0;
    for (char ch : h.internalSchema)
        underscores += ch == '_';
    h.embeddedSchema = underscores == 3;
}

void requireEmbedded(const Header& h) {
    if (!h.embeddedSchema)
        throw Fail{"legacy-schema-unsupported",
                   "schema key '" + h.internalSchema + "' has no embedded schema; its layouts are not available", -1};
    // The layout tables describe the V13 base schema (SCH_13006); an embedded schema over another base
    // cannot be decoded with them, and an unchanged-layout record would not reveal the difference.
    const size_t lastUnderscore = h.internalSchema.rfind('_');
    if (lastUnderscore == std::string::npos || h.internalSchema.compare(lastUnderscore + 1, std::string::npos, "13006") != 0)
        throw Fail{"unsupported-base-schema",
                   "schema key '" + h.internalSchema + "' is not based on the V13 base schema (13006) that the layout tables describe", -1};
    if (h.userFieldSize != 0)
        throw Fail{"user-fields-unsupported", "files with user fields are not supported", -1};
}

} // namespace

File readText(std::string_view bytes, const LayoutTable& base, const Options& options) {
    File file;
    file.header.transport = Transport::Text;
    auto diag = [&](const std::string& code, const std::string& msg) {
        file.diagnostics.push_back({code, msg, -1, static_cast<int64_t>(file.nodes.size())});
    };

    // Some exporters write the transmit data without the common header (the "**ABC...**PART1;...**END_OF_HEADER" block).
    // Accept a bare "T<len> : TRANSMIT FILE ..." start; the common schema is then simply unknown.
    const size_t marker = bytes.find("**END_OF_HEADER");
    size_t bodyStart = 0;
    if (marker == std::string_view::npos) {
        size_t at = 0;
        while (at < bytes.size() && (bytes[at] == ' ' || bytes[at] == '\t' || bytes[at] == '\r' || bytes[at] == '\n'))
            ++at;
        const std::string_view rest = bytes.substr(at, 160);
        const size_t colon = rest.find(" : TRANSMIT FILE");
        const bool bare = !rest.empty() && rest[0] == 'T' && colon != std::string_view::npos && colon > 1 &&
                          rest.substr(1, colon - 1).find_first_not_of("0123456789") == std::string_view::npos;
        if (!bare) {
            diag("header-missing", "common header trailer not found");
            return file;
        }
        bodyStart = at;
    } else {
        const size_t part2 = bytes.substr(0, marker).find("SCH=");
        if (part2 != std::string_view::npos) {
            const size_t end = bytes.find(';', part2);
            file.header.commonSchema = std::string(bytes.substr(part2 + 4, end == std::string_view::npos ? 0 : end - part2 - 4));
        }
        bodyStart = bytes.find('\n', marker);
        bodyStart = bodyStart == std::string_view::npos ? bytes.size() : bodyStart + 1;
    }
    const std::string body = normalise(bytes.substr(bodyStart));
    if (body.empty()) {
        diag("truncated", "no data after the common header");
        return file;
    }
    if (body[0] != 'T') {
        diag(body.compare(0, 2, "PS") == 0 || body[0] == 'B' ? "binary-unsupported" : "unknown-transport",
             "text transport expected; use readNeutralBinary for binary data");
        return file;
    }

    TextSource s(body, options.limits, 1);
    std::unordered_set<int64_t> seen;
    std::vector<std::tuple<int64_t, int, int>> pointers;
    int64_t lastType = -1;
    try {
        file.header.modellerString = s.longString();
        parseModellerVersion(file.header);
        file.header.internalSchema = s.longString();
        checkSchemaKey(file.header);
        if (file.header.embeddedSchema)
            file.header.nodeTypeLimit = static_cast<int>(s.headerInt());
        file.header.userFieldSize = static_cast<int>(s.headerInt());
        const LayoutTable* external = nullptr;
        if (!file.header.embeddedSchema && options.externalSchema) {
            const size_t us = file.header.internalSchema.rfind('_');
            const int number = us == std::string::npos ? 0 : std::atoi(file.header.internalSchema.c_str() + us + 1);
            if (number > 0)
                external = options.externalSchema(number);
        }
        if (external) {
            if (file.header.userFieldSize != 0)
                throw Fail{"user-fields-unsupported", "files with user fields are not supported", -1};
            file.header.externalSchemaUsed = true;
        } else {
            requireEmbedded(file.header);
        }
        decodeRecords(file, s, external ? *external : base, options, &lastType, &seen, &pointers, external != nullptr);
        file.status = Status::Complete;
    } catch (const Fail& f) {
        recordFailure(file, f, lastType);
    }
    finishPointers(file, seen, pointers);
    return file;
}

File readNeutralBinary(std::string_view bytes, const LayoutTable& base, const Options& options) {
    File file;
    file.header.transport = Transport::NeutralBinary;
    auto diag = [&](const std::string& code, const std::string& msg) {
        file.diagnostics.push_back({code, msg, -1, 0});
    };
    if (bytes.size() < 4) {
        diag("truncated", "no data");
        return file;
    }
    if (bytes.substr(0, 4) != std::string_view("PS\0\0", 4)) {
        diag(bytes[0] == 'P' && bytes[1] == 'S' ? "binary-variant-unsupported" : "unknown-transport",
             "neutral binary flag sequence 'P' 'S' 0 0 expected");
        return file;
    }

    BinarySource s(bytes, options.limits, 4);
    std::unordered_set<int64_t> seen;
    std::vector<std::tuple<int64_t, int, int>> pointers;
    int64_t lastType = -1;
    try {
        file.header.modellerString = s.longString(s.headerShort());
        parseModellerVersion(file.header);
        file.header.internalSchema = s.longString();
        checkSchemaKey(file.header);
        if (file.header.embeddedSchema)
            file.header.nodeTypeLimit = static_cast<int>(s.headerShort());
        file.header.userFieldSize = static_cast<int>(s.headerInt());
        requireEmbedded(file.header);
        decodeRecords(file, s, base, options, &lastType, &seen, &pointers);
        file.status = Status::Complete;
    } catch (const Fail& f) {
        recordFailure(file, f, lastType);
    }
    finishPointers(file, seen, pointers);
    return file;
}

} // namespace Xt
