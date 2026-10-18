#pragma once

#include <QUuid>
#include <QString>
#include <QSet>
#include <QColor>
#include <QVector3D>
#include <QVector4D>

#include "GltfCameraData.h"

// A named, user-saved "configuration" combining camera pose + mesh
// visibility + active selection + presentation state - "Selection -> Save
// Scene State..." (also the States panel's own Save button). Document-level,
// same reasoning as SelectionSetData.h's SelectionSet: not owned by any
// single loaded file.
//
// Unlike a SelectionSet (selection only) or a captured view (camera only),
// a SceneState snapshots all of this together so recalling it jumps back to
// a complete "way of looking at the scene" in one click - e.g. "Exploded for
// assembly review" vs. "Interior only" vs. "Presentation angle".
struct SceneState
{
	QUuid id;
	QString name;

	// A full private snapshot (see ViewportWidget::captureCurrentCameraEntry()),
	// NOT an index into SceneGraph's captured-views bucket - deleting or
	// reordering Cameras-tab captured views must never invalidate a saved
	// scene state.
	GltfCameraEntry camera;

	// Full visibility/selection sets, not deltas - recalling a scene state
	// restores these exactly, unlike SelectionSet recall which only ever
	// reveals additional hidden members without hiding anything.
	QSet<QUuid> visibleMeshUuids;
	QSet<QUuid> selectedMeshUuids;

	// -------------------------------------------------------------------
	// Presentation state - originally a curated subset (mode/RT/skybox),
	// widened to mirror the FULL set of fields ModelViewer's own per-document
	// "viewerState" block already saves/restores on MVF load (see
	// buildMVFPackage()'s viewerState.insert(...) calls and the matching load
	// block, both in ModelViewer.cpp). That block already proves this exact
	// field list is the right one - it's the app's own definition of
	// "everything about a document's presentation that isn't geometry" - so
	// duplicating it here isn't open-ended scope creep, it's reusing an
	// already-shipped, already-correct list verbatim (same field names, same
	// getters/setters, same JSON keys, so a saved MVF's viewerState and a
	// SceneState's presentation values are directly comparable). Excluded
	// deliberately: cameraUpAxisZUp/projection/cameraMode - those are camera
	// navigation settings, not environment/rendering ones, and camera pose
	// itself is already fully captured by the `camera` field above.
	// -------------------------------------------------------------------

	// ViewportWidget::DisplayMode (Shaded/Hollow Mesh/Mesh Edges/Wireframe/
	// Shaded+Edges), stored as int rather than including ViewportWidget.h
	// here (a large header - see MaterialGrouping-style precedent elsewhere
	// in this codebase for storing an enum as int in a lightweight data
	// struct to avoid heavy header coupling). Cast at the ViewportWidget.cpp/
	// ModelViewer.cpp call sites, which already include the real header.
	int displayMode = 0; // DisplayMode::SHADED

	// One of "ADS" / "PBR" / "RayTraced" - the exact strings
	// ModelViewer::onRenderingModeSelected() already accepts, so recall can
	// call that single entry point directly (it also keeps the toolbar's
	// active-mode indicator in sync, unlike setting ViewportWidget's
	// RenderingMode enum directly). RAY_TRACED is deliberately not stored
	// as a RenderEnums.h RenderingMode value - see that enum's own doc
	// comment: ray-traced is a UI-level/session concept tracked separately
	// via ViewportWidget::isRayTracedRenderingModeArmed(), not a value
	// SceneRenderController::renderingMode() itself ever holds.
	QString renderingMode = QStringLiteral("PBR");

	// RenderEnums.h's GroundMode (None/Floor/Grid/InfinitePlane), stored as
	// int for the same header-weight reason as displayMode above (this one
	// IS a small, dependency-free header, but ViewportWidget.h itself isn't,
	// and this struct shouldn't need either just to name a value). Genuinely
	// independent of renderingMode, not merely implied by it - switching
	// rendering mode only re-asserts a canonical DEFAULT ground mode (Floor
	// for realistic shading, InfinitePlane for Ray-Traced - see
	// VisualizationEnvironmentPanel::onDisplayModeChanged()/
	// applyRayTracedGroundDefaultsOnce()), but the user can freely override
	// it afterward and it sticks until the next mode switch - so recall has
	// to restore this explicitly, AFTER restoring renderingMode, to correctly
	// reproduce either the default or an override the user actually had.
	int groundMode = 1; // GroundMode::Floor

	bool skyBoxShown = true;
	bool skyBoxHDRIEnabled = true;

	// Which skybox/HDRI preset is actually loaded, captured via
	// ViewportWidget::getCurrentSkyboxFolder() (the folder path
	// SceneRenderController itself considers current), NOT the combo box's
	// selection index - this codebase already documents (see ModelViewer's
	// constructor, seeding the default HDRI/LDRI index from Settings) that
	// index order is fragile because the scanned preset folder list can
	// change if presets are added/removed. Storing the path also covers a
	// custom (non-preset) folder picked via "Select Custom Map", which an
	// index or preset-name match couldn't identify at all. Restored via
	// ViewportWidget::setSkyBoxTextureFolder() followed by
	// VisualizationEnvironmentPanel::syncSkyBoxSelectionSilently() to keep
	// the panel's own combo/index bookkeeping in sync without a redundant
	// (and visibly flickery) reload of the texture that's already loading.
	QString skyBoxFolderPath;

	// Skybox appearance controls (comboBoxSkyBoxRotation preset + fine slider
	// combined into one angle - see VisualizationEnvironmentPanel::
	// restoreSkyBoxRotationDegrees()'s doc comment for how a single degrees
	// value decomposes back into both controls).
	int skyBoxBlurPercent = 0;
	double skyBoxFOV = 45.0;
	double skyBoxZRotationDegrees = 0.0;

	// Floor / shadow-catcher.
	bool floorTextureShown = false;
	// Which image file is shown on the floor, if any - see ViewportWidget::
	// setFloorTextureFromPath()'s doc comment. Empty means no texture was
	// ever explicitly loaded for this state (floorTextureShown would also be
	// false in that case, but they're independent fields for the same reason
	// skyBoxShown/skyBoxFolderPath are: "shown" and "which one" don't have to
	// change together).
	QString floorTexturePath;
	double floorTexRepeatS = 1.0;
	double floorTexRepeatT = 1.0;
	// UI-scale percent (0-100), matching ViewportWidget::getFloorOffsetPercent()/
	// setFloorOffsetPercent()'s own units - NOT the 0-1 fraction
	// SceneRenderController stores internally (see getFloorOffsetPercent()'s
	// doc comment for why the two differ).
	double floorOffsetPercent = 0.0;
	// AdaptiveShadowMapper::QualityLevel, stored as int for the same reason
	// as displayMode/groundMode above - AdaptiveShadowMapper.h is small and
	// dependency-free, but this struct still shouldn't need to include
	// anything just to name a value the ModelViewer.cpp/ViewportWidget.cpp
	// call sites (which already include the real header) can cast back.
	int shadowQuality = 1; // AdaptiveShadowMapper::MEDIUM_QUALITY
	bool reflectionsEnabled = false;
	bool shadowsEnabled = true;
	bool selfShadowsEnabled = true;
	float shadowCatcherDarkness = 0.5f;
	QVector3D shadowCatcherBaseColor;
	float shadowCatcherMetalness = 0.0f;
	float shadowCatcherRoughness = 1.0f;

	// Environment/IBL lighting. Exposure is stored as log2 "stops", matching
	// ViewportWidget::setEnvMapExposure()/setIBLExposure()'s own parameter
	// (they internally do pow(2, stops)) - storing the raw linear exposure
	// instead would need the same conversion done twice for no benefit.
	bool environmentEnabled = true;
	bool iblEnabled = false;
	double envMapExposureStops = 0.0;
	double iblExposureStops = 0.0;

	// Punctual/default lighting.
	bool defaultLightsEnabled = true;
	bool punctualLightsEnabled = true;
	bool showLights = false;
	QVector4D defaultLightColor = QVector4D(1.0f, 1.0f, 1.0f, 1.0f);
	QVector3D defaultLightOffset;

	// HDR tone mapping / gamma.
	bool hdrToneMapping = true;
	int hdrToneMappingMode = 0; // HDRToneMapMode's first value
	bool gammaCorrection = true;
	double screenGamma = 2.2;

	QColor bgTopColor;
	QColor bgBotColor;
};
