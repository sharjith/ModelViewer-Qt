// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
//
// Stage X1 of the Parasolid X_T importer plan: transport and record decoding only.
// No geometry, no OCCT, no Qt. See research/parasolid/PARASOLID_IMPLEMENTATION_PLAN.md.
#pragma once

#include "XtLayout.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Xt {

enum class Status { Complete, Failed };
enum class Transport { Text, NeutralBinary };

struct Diagnostic {
    std::string code;      // stable machine-readable cause
    std::string message;   // human-readable detail
    int64_t nodeType = -1; // offending node type, when known
    int64_t recordIndex = -1; // number of records decoded before the problem
};

struct Limits {
    size_t maxRecords = 50'000'000;
    int64_t maxVariableCount = 100'000'000; // elements in one variable-length node
    int64_t maxStringLength = 1'000'000;
    size_t maxValues = 1'000'000'000;       // total decoded scalar values
};

struct Options {
    Limits limits;
    // Return true to cancel. Polled about every 4096 records.
    std::function<bool()> cancelled;
    // Keep decoded field values (needed from stage X2). Off: count only.
    bool keepValues = true;
    // Layouts for a file whose internal schema key has no embedded schema (for example SCH_3700000_36001): called with
    // the schema number (36001); return null when the schema is not available. Not called for embedded schemas.
    std::function<const LayoutTable*(int)> externalSchema;
};

struct Header {
    Transport transport = Transport::Text;
    std::string modellerString;  // ": TRANSMIT FILE created by modeller version ..."
    int64_t modellerVersion = 0;
    std::string commonSchema;    // SCH= from the human-readable header, may be empty
    std::string internalSchema;  // e.g. SCH_3801231_38101_13006
    bool embeddedSchema = false; // internal key carries a base-schema suffix
    bool externalSchemaUsed = false; // layouts came from Options::externalSchema
    int nodeTypeLimit = 0;       // only when embeddedSchema
    int userFieldSize = 0;
    bool partition = false;      // partition file marker in the modeller string
};

struct Node {
    int type = 0;
    int64_t index = 0;
    size_t valueOffset = 0; // into File::values
    size_t valueCount = 0;
};

struct File {
    Header header;
    Status status = Status::Failed;
    std::vector<Diagnostic> diagnostics;

    // Effective layout per node type used by this file (after applying edit scripts).
    std::map<int, NodeLayout> layouts;
    // Node types defined inside the file rather than in the base table.
    std::map<int, std::string> inStreamTypes;

    std::vector<Node> nodes;
    // Decoded scalars, one per field element; vectors and boxes are expanded; chars hold
    // their character code; an unset value is NaN.
    std::vector<double> values;
    std::map<std::string, size_t> countsByName;

    // Bytes left after the terminator. Always 0 for text; for binary data taken from a JT
    // element it is the integer attribute data that follows the XT stream.
    size_t trailingBytes = 0;

    // Scalar pointers that are positive but do not name any decoded node (the format
    // allows a few; the count is a consistency signal, not an error).
    size_t unresolvedPointers = 0;
    std::map<std::string, size_t> unresolvedByField; // "NODE.field" -> count
};

// Decode a text X_T file held in memory. Never throws for malformed input; check
// File::status and File::diagnostics. Unsuffixed legacy schema keys are refused with a
// diagnostic.
File readText(std::string_view bytes, const LayoutTable& base, const Options& options = {});

// Decode neutral-binary XT data. `bytes` must start at the flag sequence 'P' 'S' 0 0, as
// in the XT data carried by a JT XT B-Rep element (after the element's own header fields).
// Bytes after the terminator are allowed and reported in File::trailingBytes. Use
// jtEmbeddedBaseLayouts() (XtJtLayouts.h) as `base` for data taken from JT files.
File readNeutralBinary(std::string_view bytes, const LayoutTable& base, const Options& options = {});

} // namespace Xt
