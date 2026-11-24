#pragma once

#include "IModelWriter.h"

// Writes glTF 2.0: one binary .glb (format id "glb2") or .gltf + .bin ("gltf2").
//
// Stage 2a: geometry and hierarchy only (positions, normals, texture coordinates, vertex colours that are not just white,
// indices, node transforms, mesh sharing between nodes). Materials, textures, extensions, lights, cameras, variants and
// animation follow in the next stages of research/export/EXPORTERS_PLAN.md (section 8.4); until then a file from this
// writer has the default material.
class GltfWriter : public IModelWriter
{
public:
    ExportResult write(const ExportRequest& request) const override;
};
