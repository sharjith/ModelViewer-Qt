// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
//
// Stage X1 tests: synthetic, original fixtures only. The layouts used here are a tiny
// invented table (not a real schema); real files and real layouts stay local.
#include "XtJtLayouts.h"
#include "XtModel.h"
#include "XtReader.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>

static int g_failures = 0;
#define CHECK(cond)                                                                              \
    do {                                                                                         \
        if (!(cond)) {                                                                           \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                \
            ++g_failures;                                                                        \
        }                                                                                        \
    } while (0)

namespace {

// Invented layouts. Type numbers are arbitrary and local to these tests.
const char* kLayouts =
    "# synthetic\n"
    "29 PT node_id:d owner:p next:p pvec:v\n"
    "83 NUMS values:f[]\n"
    "90 MIX count:d tag:c flag:l tol:f box:b\n";

std::string lenStr(const std::string& s) { return std::to_string(s.size()) + " " + s; }

std::string header(const std::string& schema = "SCH_3801231_38101") {
    return "**ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz**************\n"
           "**PARASOLID !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~0123456789**************\n"
           "**PART1;\nAPPL=test;\nFORMAT=text;\n"
           "**PART2;\nSCH=" + schema + ";\nUSFLD_SIZE=0;\n**PART3;\n"
           "**END_OF_HEADER***************************************************\n";
}

std::string prefix(const std::string& internal = "SCH_3801231_38101_13006") {
    return "T" + lenStr(": TRANSMIT FILE created by modeller version 3801231") + lenStr(internal) + "249 0 ";
}

Xt::LayoutTable layouts() {
    Xt::LayoutTable t;
    std::string error;
    CHECK(t.parse(kLayouts, &error));
    return t;
}

bool hasCode(const Xt::File& f, const std::string& code) {
    for (const auto& d : f.diagnostics)
        if (d.code == code)
            return true;
    return false;
}

// Wraps a long record across physical lines the way a producer would. A space is never
// left at the end of a physical line (it would be dropped by the reader's rules); it moves
// to the start of the next one.
std::string wrap(const std::string& s, size_t width) {
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        size_t cut = std::min(width, s.size() - i);
        while (cut > 1 && i + cut < s.size() && s[i + cut - 1] == ' ')
            --cut;
        out += s.substr(i, cut);
        out += '\n';
        i += cut;
    }
    return out;
}

void testLayoutParser() {
    Xt::LayoutTable t;
    std::string error;
    CHECK(!t.parse("29 PT a:z\n", &error));
    CHECK(!error.empty());
    CHECK(!t.parse("29 PT a:d\n29 PT2 b:d\n", &error)); // duplicate type
    CHECK(!t.parse("1 TERM a:d\n", &error));            // type 1 is the terminator
    CHECK(t.parse("# comment\n\n29 PT a:d b:v c:f[] \r\n", &error));
    CHECK(t.size() == 1);
    CHECK(t.find(29) && t.find(29)->fields.size() == 3 && t.find(29)->fields[2].count == 1);
    CHECK(t.find(30) == nullptr);
}

void testBaseLayoutRecords() {
    // Two points. The first uses the unchanged-layout flag (255); the second repeats the
    // type. The second node's `next` pointer names no node (99), its `owner` names node 1.
    const std::string good = prefix() +
        "29 255 1 7 0 0 .5 -.25 1e3 " + "29 2 8 1 99 1 2 3 " + "1 0";
    const auto f = Xt::readText(header() + wrap(good, 37), layouts());
    CHECK(f.status == Xt::Status::Complete);
    CHECK(f.nodes.size() == 2);
    CHECK(f.header.internalSchema == "SCH_3801231_38101_13006");
    CHECK(f.header.embeddedSchema);
    CHECK(f.header.nodeTypeLimit == 249);
    CHECK(f.header.commonSchema == "SCH_3801231_38101");
    CHECK(f.header.modellerVersion == 3801231);
    CHECK(f.countsByName.at("PT") == 2);
    CHECK(f.values.size() == 12);
    CHECK(f.values[3] == 0.5 && f.values[4] == -0.25 && f.values[5] == 1000.0);
    CHECK(f.values[6] == 8 && f.values[7] == 1 && f.values[8] == 99);
    CHECK(f.unresolvedPointers == 1);
    CHECK(f.unresolvedByField.at("PT.next") == 1);
}

void testUnsetAndVariableAndMixed() {
    // Unset values are a bare '?' with no space after it. The variable-length node carries
    // its element count before the index. Chars and logicals are not space-terminated.
    const std::string body = prefix() +
        "83 255 3 5 1 ?3 " +                    // NUMS, 3 elements, index 5: 1, unset, 3
        "90 255 9 2 xT?1 2 3 4 5 6 " +          // MIX: count 2, 'x', 'T', unset tol, box
        "1 0";
    const auto f = Xt::readText(header() + body, layouts());
    CHECK(f.status == Xt::Status::Complete);
    CHECK(f.nodes.size() == 2);
    CHECK(f.nodes[0].valueCount == 3);
    CHECK(std::isnan(f.values[1]));
    CHECK(f.values[2] == 3.0);
    CHECK(f.nodes[1].valueCount == 10);
    CHECK(f.values[f.nodes[1].valueOffset + 1] == 'x');
    CHECK(f.values[f.nodes[1].valueOffset + 2] == 'T');
    CHECK(std::isnan(f.values[f.nodes[1].valueOffset + 3]));
}

void testEditScriptAppend() {
    // Type 29 has four fields in the test layout. Copy all four and append one integer.
    const std::string body = prefix() +
        "29 5 CCCCA5 extra0 0 1 dZ" + "1 7 0 0 .5 .5 .5 42 " + "1 0";
    const auto f = Xt::readText(header() + body, layouts());
    CHECK(f.status == Xt::Status::Complete);
    CHECK(f.layouts.at(29).fields.size() == 5);
    CHECK(f.layouts.at(29).fields.back().name == "extra");
    CHECK(f.values.size() == 7);
    CHECK(f.values.back() == 42.0);
}

void testInStreamType() {
    // A type not in the base table, defined completely inside the file.
    const std::string body = prefix() +
        "150 2 " + lenStr("EXTRA") + lenStr("an extra node") +
        lenStr("a") + "0 0 " + lenStr("d") +
        lenStr("b") + "0 0 " + lenStr("d") +
        "3 11 22 " + "150 4 33 44 " + "1 0";
    const auto f = Xt::readText(header() + body, layouts());
    CHECK(f.status == Xt::Status::Complete);
    CHECK(f.inStreamTypes.count(150) == 1);
    CHECK(f.countsByName.at("EXTRA") == 2);
}

void testRefusals() {
    {   // legacy unsuffixed schema key
        const auto f = Xt::readText(header("SCH_800000_8008") +
                                    "T" + lenStr(": TRANSMIT FILE created by modeller version 3601212") +
                                    lenStr("SCH_800000_8008") + "0 10 1 0", layouts());
        CHECK(f.status == Xt::Status::Failed);
        CHECK(hasCode(f, "legacy-schema-unsupported"));
    }
    {   // embedded schema over a different base than the layout tables describe
        const auto f = Xt::readText(header("SCH_3801231_38101_9999") + prefix("SCH_3801231_38101_9999") + "1 0", layouts());
        CHECK(f.status == Xt::Status::Failed && hasCode(f, "unsupported-base-schema"));
    }
    {   // binary transport
        const auto f = Xt::readText(header() + std::string("PS\0\0", 4) + "rest", layouts());
        CHECK(f.status == Xt::Status::Failed && hasCode(f, "binary-unsupported"));
    }
    {   // no common header
        const auto f = Xt::readText("T51 no header", layouts());
        CHECK(hasCode(f, "header-missing"));
    }
    {   // unchanged-layout flag on a type the table does not know
        const auto f = Xt::readText(header() + prefix() + "151 255 1 0 0 1 0", layouts());
        CHECK(f.status == Xt::Status::Failed && hasCode(f, "unknown-base-layout"));
    }
    {   // edit script that does not consume the base layout
        const auto f = Xt::readText(header() + prefix() + "29 1 CZ1 7 1 0", layouts());
        CHECK(f.status == Xt::Status::Failed && hasCode(f, "base-layout-mismatch"));
    }
    {   // missing terminator
        const auto f = Xt::readText(header() + prefix() + "29 255 1 7 0 0 .5 .5 .5 ", layouts());
        CHECK(f.status == Xt::Status::Failed && hasCode(f, "truncated"));
    }
    {   // data after the terminator
        const auto f = Xt::readText(header() + prefix() + "1 0 29 255", layouts());
        CHECK(f.status == Xt::Status::Failed && hasCode(f, "trailing-data"));
    }
    {   // not a number
        const auto f = Xt::readText(header() + prefix() + "29 255 1 7 0 0 abc 1 2 1 0", layouts());
        CHECK(f.status == Xt::Status::Failed && hasCode(f, "bad-number"));
    }
}

void testLimitsAndCancel() {
    {   // absurd variable-length count must not allocate
        Xt::Options o;
        o.limits.maxVariableCount = 10;
        const auto f = Xt::readText(header() + prefix() + "83 255 99999999 5 1 1 0", layouts(), o);
        CHECK(f.status == Xt::Status::Failed && hasCode(f, "limit"));
    }
    {   // absurd string length
        Xt::Options o;
        o.limits.maxStringLength = 8;
        const auto f = Xt::readText(header() + "T" + lenStr("0123456789") + lenStr("SCH_1_2_3") + "0 0 ", layouts(), o);
        CHECK(f.status == Xt::Status::Failed && hasCode(f, "limit"));
    }
    {   // record limit
        Xt::Options o;
        o.limits.maxRecords = 1;
        const auto f = Xt::readText(header() + prefix() + "83 255 1 5 1 83 1 6 2 1 0", layouts(), o);
        CHECK(f.status == Xt::Status::Failed && hasCode(f, "limit"));
    }
    {   // cancellation is polled; a callback that always cancels stops a long stream
        std::string body = prefix();
        body += "83 255 1 1 1.5 ";
        for (int i = 2; i <= 9000; ++i)
            body += "83 1 " + std::to_string(i) + " 1.5 ";
        body += "1 0";
        Xt::Options o;
        o.cancelled = [] { return true; };
        const auto f = Xt::readText(header() + body, layouts(), o);
        CHECK(f.status == Xt::Status::Failed && hasCode(f, "cancelled"));
        CHECK(f.nodes.size() < 9000);
    }
}

void testTruncationAtEveryBoundary() {
    const std::string good = header() + prefix() + "29 255 1 7 0 0 .5 -.25 1e3 83 255 3 5 1 2 3 1 0";
    CHECK(Xt::readText(good, layouts()).status == Xt::Status::Complete);
    // Every proper prefix after the header must fail without crashing, except the few
    // prefixes that happen to end exactly at a record boundary with a terminator.
    const size_t start = header().size();
    for (size_t n = start; n < good.size(); ++n) {
        const auto f = Xt::readText(std::string_view(good).substr(0, n), layouts());
        CHECK(f.status == Xt::Status::Failed);
    }
}


// ---- neutral binary ------------------------------------------------------------------

struct Bin {
    std::string s;
    Bin& u8(unsigned v) { s += static_cast<char>(v); return *this; }
    Bin& i16(int v) { s += static_cast<char>((v >> 8) & 0xFF); s += static_cast<char>(v & 0xFF); return *this; }
    Bin& i32(int32_t v) {
        for (int sh = 24; sh >= 0; sh -= 8) s += static_cast<char>((static_cast<uint32_t>(v) >> sh) & 0xFF);
        return *this;
    }
    Bin& f64(double d) {
        uint64_t v; std::memcpy(&v, &d, 8);
        for (int sh = 56; sh >= 0; sh -= 8) s += static_cast<char>((v >> sh) & 0xFF);
        return *this;
    }
    Bin& idx(int64_t i) { // pointer-index encoding
        if (i < 32767) return i16(static_cast<int>(i + 1));
        i16(-static_cast<int>(i % 32767 + 1));
        return i16(static_cast<int>(i / 32767));
    }
    Bin& str8(const std::string& t) { u8(static_cast<unsigned>(t.size())); s += t; return *this; }
    Bin& raw(const std::string& t) { s += t; return *this; }
};

Bin binPrefix() {
    const std::string modeller = ": TRANSMIT FILE created by modeller version 3801231";
    const std::string schema = "SCH_3801231_38101_13006";
    Bin b;
    b.raw(std::string("PS  ", 4)).i16(static_cast<int>(modeller.size())).raw(modeller)
        .i32(static_cast<int32_t>(schema.size())).raw(schema).i16(249).i32(0);
    return b;
}

void testBinaryBasic() {
    Bin b = binPrefix();
    // PT in unchanged form: node_id, owner (big index 40000, no such node), next 0, pvec
    b.i16(29).u8(0xFF).idx(1).i32(7).idx(40000).idx(0).f64(0.5).f64(-0.25).f64(1000.0);
    // NUMS variable length (3 elements) with an unset real
    b.i16(83).u8(0xFF).i32(3).idx(2).f64(1.0).f64(-3.14158e13).f64(3.0);
    b.i16(1).idx(0);
    const auto f = Xt::readNeutralBinary(b.s, layouts());
    CHECK(f.status == Xt::Status::Complete);
    CHECK(f.header.transport == Xt::Transport::NeutralBinary);
    CHECK(f.header.internalSchema == "SCH_3801231_38101_13006");
    CHECK(f.header.nodeTypeLimit == 249);
    CHECK(f.header.modellerVersion == 3801231);
    CHECK(f.nodes.size() == 2);
    CHECK(f.values[0] == 7 && f.values[1] == 40000 && f.values[3] == 0.5 && f.values[5] == 1000.0);
    CHECK(f.nodes[1].valueCount == 3 && std::isnan(f.values[7]) && f.values[8] == 3.0);
    CHECK(f.unresolvedPointers == 1 && f.unresolvedByField.at("PT.owner") == 1);
    CHECK(f.trailingBytes == 0);
}

void testBinaryTrailingAndEdit() {
    Bin b = binPrefix();
    // PT edited: copy 4 base fields, append one int field "extra"
    b.i16(29).u8(5).u8('C').u8('C').u8('C').u8('C').u8('A').str8("extra").i16(0).idx(0).str8("d").u8('Z');
    b.idx(1).i32(7).idx(0).idx(0).f64(1).f64(2).f64(3).i32(-32764); // extra is the unset int
    b.i16(1).idx(0);
    b.raw("tail!"); // integer attribute data after the XT stream in a JT element
    const auto f = Xt::readNeutralBinary(b.s, layouts());
    CHECK(f.status == Xt::Status::Complete);
    CHECK(f.trailingBytes == 5);
    CHECK(f.layouts.at(29).fields.size() == 5 && f.layouts.at(29).fields.back().name == "extra");
    CHECK(std::isnan(f.values[6]));
}

void testBinaryRefusalsAndTruncation() {
    CHECK(hasCode(Xt::readNeutralBinary("T text", layouts()), "unknown-transport"));
    CHECK(hasCode(Xt::readNeutralBinary(std::string("PS rest", 8), layouts()), "binary-variant-unsupported"));
    {   // unsuffixed schema key
        const std::string schema = "SCH_3210000_32101";
        Bin b;
        b.raw(std::string("PS  ", 4)).i16(3).raw("abc").i32(static_cast<int32_t>(schema.size())).raw(schema).i32(0).i16(1).idx(0);
        CHECK(hasCode(Xt::readNeutralBinary(b.s, layouts()), "legacy-schema-unsupported"));
    }
    Bin good = binPrefix();
    good.i16(29).u8(0xFF).idx(1).i32(7).idx(0).idx(0).f64(1).f64(2).f64(3).i16(1).idx(0);
    CHECK(Xt::readNeutralBinary(good.s, layouts()).status == Xt::Status::Complete);
    for (size_t n = 4; n < good.s.size(); ++n)
        CHECK(Xt::readNeutralBinary(std::string_view(good.s).substr(0, n), layouts()).status == Xt::Status::Failed);
}

void testJtLayoutTable() {
    const Xt::LayoutTable& t = Xt::jtEmbeddedBaseLayouts();
    CHECK(t.size() > 40);
    CHECK(t.find(12) && t.find(12)->name == "BODY");
    CHECK(t.find(14) && t.find(14)->name == "FACE");
    CHECK(t.find(83) && t.find(83)->fields.back().count == 1);
}

// ---- stage X2: model view and topology validation ------------------------------------
// Invented, reduced layouts with the field names the validator reads (not a real schema).
const char* kTopoLayouts =
    "12 BODY shell:p\n"
    "13 SHELL face:p\n"
    "14 FACE loop:p shell:p next:p\n"
    "15 LOOP fin:p face:p next:p\n"
    "16 EDGE fin:p\n"
    "17 FIN loop:p forward:p backward:p other:p edge:p vertex:p\n"
    "18 VERTEX fin:p\n";

// One face, one loop of two fins on one edge and one vertex. Indexes: loop 1, face 2,
// fins 3 and 4, edge 5, vertex 6, shell 7, body 8. `fin4Loop` lets a test break it.
std::string topoStream(int fin4Loop) {
    return prefix() + "15 255 1 3 2 0 " + "14 255 2 1 7 0 " + "17 255 3 1 4 4 4 5 6 " +
           "17 4 " + std::to_string(fin4Loop) + " 3 3 3 5 6 " + "16 255 5 3 " + "18 255 6 3 " +
           "13 255 7 2 " + "12 255 8 7 " + "1 0";
}

void testTopologyValidation() {
    Xt::LayoutTable topo;
    std::string error;
    CHECK(topo.parse(kTopoLayouts, &error));
    {
        const auto f = Xt::readText(header() + topoStream(1), topo);
        CHECK(f.status == Xt::Status::Complete);
        const Xt::Model m(f);
        const Xt::Slot s = m.slot("FIN", "edge");
        CHECK(s.ok());
        CHECK(m.find(3) != nullptr && m.pointer(*m.find(3), s) == 5);
        CHECK(!m.slot("FIN", "nonexistent").ok() && !m.slot("NOPE", "edge").ok());
        const Xt::TopologyReport r = Xt::validateTopology(m);
        CHECK(r.bodies == 1 && r.shells == 1 && r.faces == 1 && r.loops == 1);
        CHECK(r.fins == 2 && r.edges == 1 && r.vertices == 1);
        CHECK(r.consistent());
        CHECK(r.finsPerEdge.count(2) == 1 && r.finsPerEdge.at(2) == 1);
        CHECK(r.eulerPerShell.size() == 1 && r.eulerPerShell.count(1) == 1); // V 1 - E 1 + F 1
    }
    {   // second fin claims a loop that does not exist
        const auto f = Xt::readText(header() + topoStream(99), topo);
        CHECK(f.status == Xt::Status::Complete);
        const Xt::TopologyReport r = Xt::validateTopology(Xt::Model(f));
        CHECK(!r.consistent());
        CHECK(r.danglingByField.count("FIN.loop") == 1);
    }
    {   // second fin claims another existing node (the edge) as its loop
        const auto f = Xt::readText(header() + topoStream(5), topo);
        const Xt::TopologyReport r = Xt::validateTopology(Xt::Model(f));
        CHECK(!r.consistent());
        CHECK(r.wrongTypeByField.count("FIN.loop") == 1);
    }
}

} // namespace

// Prints each test's name before it runs so a hard crash shows which one, and turns an
// escaped exception into a reported failure instead of terminating the process.
static void run(const char* name, void (*test)()) {
    std::fprintf(stderr, "running %s\n", name);
    std::fflush(stderr);
    try {
        test();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL %s: exception: %s\n", name, e.what());
        ++g_failures;
    } catch (...) {
        std::fprintf(stderr, "FAIL %s: unknown exception\n", name);
        ++g_failures;
    }
}

#define RUN(t) run(#t, t)

int main() {
    RUN(testLayoutParser);
    RUN(testBaseLayoutRecords);
    RUN(testUnsetAndVariableAndMixed);
    RUN(testEditScriptAppend);
    RUN(testInStreamType);
    RUN(testRefusals);
    RUN(testLimitsAndCancel);
    RUN(testTruncationAtEveryBoundary);
    RUN(testBinaryBasic);
    RUN(testBinaryTrailingAndEdit);
    RUN(testBinaryRefusalsAndTruncation);
    RUN(testJtLayoutTable);
    RUN(testTopologyValidation);
    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("parasolid_xt_tests: all checks passed");
    return 0;
}
