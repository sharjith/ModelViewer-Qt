#pragma once

#include "PmiGeometry.h"

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
    // Recorded PMI of this node's part: the stored drawing of each entity, indexed by the entity number of the "PMI <n>" rows.
    // Null when the importer kept none.
    std::shared_ptr<const PmiGeometry> pmiGeometry;
    // True when this node declares a mass (CAD_MASS::) and some node below it does too: it is an assembly, and its mass is the whole assembly's, not
    // a part's, so no part may take it as its own (see NodeProperties::declaredMass()). Set by the importer after it has seen the whole tree.
    bool massAggregate = false;
};

using NodePropertySetPtr = std::shared_ptr<const NodePropertySet>;
// A mesh's properties, innermost node first, then each ancestor that has any. Never inherited silently: every set keeps
// the name of the node it came from.
using NodePropertyChain = std::vector<NodePropertySetPtr>;
using NodePropertyChainPtr = std::shared_ptr<const NodePropertyChain>;

// A part mass the source file declares (not computed by the viewer). Kilograms; negative when there is none.
struct DeclaredMass
{
    double  kilograms = -1.0;
    QString sourceKey;   // the property key it came from, e.g. "CAD_MASS::"
    QString sourceNode;  // the node whose property set held it
    const void* holder = nullptr;   // identity of that property set: meshes under the same part share it
    bool valid() const { return kilograms >= 0.0; }
};

// A material designation the source file declares for a part (a name such as "Steel, Mild"; free text, shown as written, never matched
// against a catalogue). Empty name when there is none.
struct DeclaredMaterial
{
    QString name;
    QString sourceKey;   // the property key it came from, e.g. "STEP_MATERIAL_NAME"
    QString sourceNode;  // the node whose property set held it
    bool valid() const { return !name.isEmpty(); }
};

// The part number a file declares for its product structure's root (not for a part inside it) and the description that goes with it, as the
// file writes them. Empty number when the format has no such field; nothing is derived from names.
struct DeclaredPartNumber
{
    QString number;
    QString description;
    QString sourceKey;   // the property key the number came from, e.g. "3DXML_V_Name"
    bool valid() const { return !number.isEmpty(); }
};

namespace NodeProperties
{
// The declared part number of the outermost node of the chain (a mesh's properties, innermost first): the product structure's root, so every mesh
// of a file gives the same answer. 3DXML: the root reference's V_Name ("3DXML_V_Name") and V_description. STEP: the root product's id ("STEP_PRODUCT_ID"). Other formats: none.
DeclaredPartNumber declaredPartNumber(const NodePropertyChain& chain);

// The declared material of a mesh's part: the first property set of the chain (innermost node first) with a non-empty "STEP_MATERIAL_NAME"
// (written by the STEP importer from OpenCASCADE's XCAF material of the part).
DeclaredMaterial declaredMaterial(const NodePropertyChain& chain);

// The declared mass of a mesh's part: the first property set of the chain (innermost node first) that is not an assembly (massAggregate) and
// has a "CAD_MASS::" holding a finite number greater than zero. A zero is not a mass (the Solid Edge JT samples write 0 for parts without a
// material). The unit is kilograms: the set's "CAD_MASS_UNITS::" must say kilograms when it is present (Solid Edge writes it; the NX samples do
// not and were checked against standard washers and a nut), any other unit is not taken. See research/materials/MATERIAL_AND_MASS_SOURCES.md.
// Nothing is guessed: no key, no value, no mass.
DeclaredMass declaredMass(const NodePropertyChain& chain);

// Upper bound on rows kept per node; a final "truncated" row is added when it is reached.
constexpr size_t MaxRowsPerNode = 4096;

// Flattens one metadata block. A JT "JTProperties" block (ordered Key/Value records) becomes one row per record; any
// other nested block becomes a single "group" row holding its entry count. Returns null when the node has no metadata.
// `pmiGeometry` (optional) is attached to the result as the node's recorded PMI drawings.
NodePropertySetPtr fromNode(const aiNode& node, std::shared_ptr<const PmiGeometry> pmiGeometry = nullptr, bool massAggregate = false);

// True when the node (or the set) has a CAD_MASS:: entry holding a finite number >= 0, whatever that number is; used to find assemblies.
bool declaresMassKey(const aiNode& node);
bool declaresMassKey(const NodePropertySet& set);

// True when the metadata block holds at least one entry.
bool hasEntries(const aiMetadata* metadata);
}
