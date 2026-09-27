#pragma once

#include "PunctualLights.h"
#include <QString>
#include <QVector>

// ---------------------------------------------------------------------------
// GltfLightData
//
// Data structures for KHR_lights_punctual extension support.
//
// A glTF file may define one or more punctual lights (directional, point, or
// spot) that are placed in the scene via node references.  Each light carries
// its GPU-ready data plus a display name and a per-light enabled flag so the
// user can activate/deactivate individual lights from the UI.
//
// These structures are populated by AssImpModelLoader (via
// MaterialProcessor::parseKHRLightsPunctual) during import and forwarded to
// SceneGraph::appendFromScene, which stores them keyed by source file path.
//
// PunctualLightsPanel reads them to build its tree widget (one top-level item
// per file, one child item per light, with individual checkboxes).
// ViewportWidget calls SceneGraph::buildEnabledLightList() to assemble the flat
// std::vector<GPULight> that is uploaded to the UBO before each frame.
//
// Important distinction
// ---------------------
// The "system default light" (ViewportWidget::_lightPosition, used for shadow map
// generation and the Blinn-Phong pass) is completely separate and is not
// represented here.  GltfLightData covers only KHR_lights_punctual lights that
// originate from a loaded model file.  If no file has punctual lights the UBO
// loop in the shader is skipped entirely; the system default light handles
// illumination on its own without any fallback GPULight being needed.
// ---------------------------------------------------------------------------

// One punctual light entry from a glTF/GLB source file.
struct GltfLightEntry
{
    // Display name from the glTF "name" field (may be empty for unnamed lights).
    QString  name;

    // GPU-ready light data (position/direction already transformed by the
    // node's world transform and the file's importCorrection / autoScale).
    GPULight gpuLight = {};

    // Whether this individual light is currently enabled.
    // Toggled by the per-light checkbox in PunctualLightsPanel.
    bool     enabled  = true;
};

// All punctual light information for one loaded glTF/GLB source file.
struct GltfLightData
{
    // Absolute path of the source file (matches SceneNode::sourceFile).
    QString               sourceFile;

    // Ordered list of lights as declared in the glTF file.
    QVector<GltfLightEntry> lights;

    bool isEmpty() const { return lights.isEmpty(); }
};
