#pragma once

// Drafting-style line weights (device pixels) shared by the viewport's line passes: object lines are the thick weight and
// everything that annotates them (dimension, extension and leader lines, arcs, text strokes, section hatching) is the thin one.
// See research/measurement/LINE_WEIGHT_STUDY.md.
namespace OverlayLineWeights
{
inline constexpr float objectLine = 1.5f; // feature edges, mesh edges
inline constexpr float wireframeOverlayLine = 1.75f; // feature-edge wireframe, heavier so it stays visible over solid rendering
inline constexpr float thinLine = 1.25f;  // dimensions, leaders, annotation strokes, section hatching
inline constexpr float hatchSpacingPx = 11.0f; // section hatch line spacing: about 3 mm on a 96 dpi screen, before device-pixel-ratio scaling
}
