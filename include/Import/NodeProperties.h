#pragma once

#include <QString>
#include <memory>
#include <vector>

struct aiNode;
struct aiMetadata;

// Read-only copy of a scene node's Assimp metadata, flattened to display strings so any importer that fills
// aiNode::mMetaData (JT properties, 3DXML references, glTF extras, ...) shows up in one generic property table.
// Nothing here is interpreted: no units, no types beyond a label, no conversion of values.
struct NodePropertyRow
{
    QString key;
    QString value;
    QString type; // "text", "integer", "real", "bool", "vector", "group"
};

struct NodePropertySet
{
    QString nodeName;
    std::vector<NodePropertyRow> rows;
};

using NodePropertySetPtr = std::shared_ptr<const NodePropertySet>;
// A mesh's properties, innermost node first, then each ancestor that has any. Never inherited silently: every set keeps
// the name of the node it came from.
using NodePropertyChain = std::vector<NodePropertySetPtr>;
using NodePropertyChainPtr = std::shared_ptr<const NodePropertyChain>;

namespace NodeProperties
{
// Upper bound on rows kept per node; a final "truncated" row is added when it is reached.
constexpr size_t MaxRowsPerNode = 4096;

// Flattens one metadata block. A JT "JTProperties" block (ordered Key/Value records) becomes one row per record; any
// other nested block becomes a single "group" row holding its entry count. Returns null when the node has no metadata.
NodePropertySetPtr fromNode(const aiNode& node);

// True when the metadata block holds at least one entry.
bool hasEntries(const aiMetadata* metadata);
}
