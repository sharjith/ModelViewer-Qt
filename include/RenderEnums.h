#pragma once

// ---------------------------------------------------------------------------
// RenderEnums.h
//
// Render-pipeline enumerations shared between ViewportWidget and
// SceneRenderController.  Extracted from ViewportWidget.h in Phase 10 of the
// mesh/render/runtime separation refactor so that SceneRenderController.h
// can include them without creating a circular dependency.
// ---------------------------------------------------------------------------

// RAY_TRACED is a UI-level/session concept, not a raw value fed to the
// raster shader's "renderingMode" uniform - the raster pass is always kept on
// PHYSICALLY_BASED_RENDERING while ray-traced mode is armed (see
// ViewportWidget::armRayTracedRenderingMode() and the design note above
// ModelViewer::onRenderingModeSelected()).
enum class RenderingMode          { ADS_BLINN_PHONG, PHYSICALLY_BASED_RENDERING, RAY_TRACED };
enum class ShadingNormalMode      { SMOOTH, FLAT };
enum class ClippingPlaneHatchMode { PROCEDURAL, TEXTURE };
enum class HatchPattern           { DIAGONAL_45 = 0, DIAGONAL_135 = 1, HORIZONTAL = 2, VERTICAL = 3, GRID = 4, DIAGONAL_CROSS = 5 };

enum class DebugOverlayMode  { BoundingBox, VertexNormals, FaceNormals };
enum class HDRToneMapMode    { KhronosPbrNeutral, ACES_Narkowicz, ACES_Hill,
                               AECS_Hill_Exposure_Boost, Uncharted2ToneMapping, Reinhard };
// InfinitePlane: path-tracer-only shadow-catcher ground - mutually exclusive
// with Floor/Grid via its own radio button (Visualization panel's Ground
// section), only selectable while Ray Traced rendering is armed. Raster has
// no equivalent (ViewportWidget's ground-drawing if/else-if chain simply
// draws nothing for this value, same as it would for an unhandled case) -
// selecting it is translated to "Floor mode + shadow-catcher shading" at the
// PT snapshot-build call site (see ViewportWidget::buildRayTracedSnapshot()
// / wherever RtFloorParams is populated), so RtSceneBuilder/the PT engines
// never need to know this enum value exists at all.
enum class GroundMode        { None = 0, Floor = 1, Grid = 2, InfinitePlane = 3 };

// Viewport enumerations (also extracted here to avoid circular includes from
// ViewportInteractionController.h; ViewportWidget.h replaces its inline definitions
// with #include "RenderEnums.h" for all of these).
enum class ViewMode          { TOP, BOTTOM, LEFT, RIGHT, FRONT, BACK,
                               ISOMETRIC, DIMETRIC, TRIMETRIC, NONE };
// The compass corner an axonometric view (isometric, dimetric or trimetric) is seen from. SE is the
// default; the others are the same view turned in 90-degree steps about the up axis. Independent of
// the axonometric type. Compass names as in engineering drawings: North = +Y in the Top view, East = +X.
enum class IsoCorner         { SE, NE, NW, SW };
inline bool isAxonometricMode(ViewMode m) { return m == ViewMode::ISOMETRIC || m == ViewMode::DIMETRIC || m == ViewMode::TRIMETRIC; }
enum class ViewProjection    { ORTHOGRAPHIC, PERSPECTIVE };
// Oblique flavour of the ORTHOGRAPHIC projection (Cavalier / Cabinet). NONE is the ordinary perpendicular
// orthographic projection. Only ever set together with ViewProjection::ORTHOGRAPHIC. It is a property of
// the projection, not of the view: it persists through orbiting, panning, zooming and every view change,
// and the face parallel to the screen keeps its true shape while depth recedes along a slanted axis.
enum class ObliqueMode       { NONE, CAVALIER, CABINET };
// Depth scale (rho) of an oblique mode: Cavalier draws depth at full scale, Cabinet at half scale.
inline float obliqueDepthScale(ObliqueMode m) { return m == ObliqueMode::CAVALIER ? 1.0f : m == ObliqueMode::CABINET ? 0.5f : 0.0f; }
// Screen angle of the receding depth axis, measured from screen-right (the classic 45 degrees).
constexpr float kObliqueAngleDegrees = 45.0f;
enum class CornerAxisPosition { TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, BOTTOM_RIGHT };
