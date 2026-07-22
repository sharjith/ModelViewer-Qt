#pragma once

#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "ShaderProgram.h"

#include <QOpenGLFunctions_4_5_Core>
#include <QString>

// ---------------------------------------------------------------------------
// RtPresenter
//
// Presents the path tracer's latest frame (RtPathTracingSession::
// latestFrame()) into the existing OpenGL viewport: uploads the linear HDR
// RGB buffer plus primary-hit alpha into a GL_RGBA32F texture (same upload pattern as
// SceneRenderController's transmission buffer) and draws it as a fullscreen
// triangle, reusing the project's existing fullscreen_triangle.vert plus a
// new small tonemap+gamma fragment shader (path_traced_present.frag).
//
// Deliberately kept separate from SceneRenderController rather than folded
// into that already-large class - path tracing is a bolt-on display mode
// layered on top of the raster pipeline, not part of what that class manages.
// ---------------------------------------------------------------------------
class RtPresenter : public QOpenGLFunctions_4_5_Core
{
public:
	// Must be called once with a current GL context. shaderBasePath is the
	// same base path SceneRenderController::initShaders() takes - this
	// resolves shaders from shaderBasePath + "shaders/<name>", matching that
	// call's convention exactly (see ViewportWidget's call site).
	bool initialize(const QString& shaderBasePath);

	// Releases GL resources. Must be called while a GL context is current
	// (mirrors SceneRenderController::cleanupGLResources()'s contract).
	void cleanup();

	// Uploads a linear HDR RGB buffer (width*height, un-tonemapped, row 0 =
	// top of image - matching RtPathTracingSession/QImage convention) and an
	// optional alpha mask as the frame to present next. Alpha is 0 where the
	// primary ray missed geometry, letting the already-rendered raster
	// gradient/skybox remain visible.
	void upload(const std::vector<glm::vec3>& rgb, int width, int height,
		const std::vector<float>* alpha = nullptr);

	// Draws the uploaded frame as a fullscreen triangle over the current
	// viewport/FBO (tonemap + gamma applied in the shader). No-op if nothing
	// has been uploaded yet or initialize() failed.
	//
	// toneMapMode mirrors RenderEnums.h's HDRToneMapMode ordinal
	// (0=KhronosPbrNeutral, 1=ACES_Narkowicz, ...) - callers pass
	// static_cast<int>(SceneRenderController::toneMappingMode()). The other
	// four parameters mirror that same controller's hdrToneMapping()/
	// gammaCorrection()/screenGamma()/iblExposure(). Passing the LIVE
	// values (rather than the shader's own hardcoded ACES-Narkowicz
	// defaults, which this used to always use regardless of what raster
	// was configured to show) keeps a path-traced-vs-raster comparison an
	// apples-to-apples exposure/contrast comparison - a real, previously-
	// deferred mismatch (see this file's git history) that visibly showed
	// as PT looking "washed out" relative to raster whenever the user had
	// a non-ACES tonemap mode selected (e.g. the default KhronosPbrNeutral).
	//
	// forceOpaque: false (default) blends normally (GL_SRC_ALPHA/GL_ONE_MINUS_
	// SRC_ALPHA) using this frame's own per-pixel alpha, letting raster show
	// through wherever the primary ray missed - the intended behavior once
	// PT has converged/settled, matching whatever raster is showing right
	// now pixel-for-pixel (see upload()'s alpha doc comment). true instead
	// overwrites unconditionally (GL_ONE/GL_ZERO, ignoring this frame's
	// alpha entirely) - ViewportWidget passes this while
	// _pathTracedInteractiveActive is true: raster keeps redrawing every
	// frame in response to live mouse movement (that's what makes camera
	// tracking itself feel responsive), but the interactive PT frame only
	// updates every so often (throttled, one chunk at a time - see
	// startInteractivePathTracedGpuSession()), so blending its sparse alpha=0
	// "miss" pixels against a continuously, independently updating raster
	// frame underneath made the model (PT-covered, alpha=1, updating slowly)
	// visibly stutter against a smoothly-moving background (raster showing
	// through, alpha=0, updating every frame) - two different motion rates
	// composited together read as broken/jerky rather than "just noisy."
	// Forcing full opacity means the ENTIRE visible image is whatever the
	// last interactive PT chunk rendered (a complete, self-consistent frame
	// for its own camera pose, background included - see
	// sampleEnvironmentBackground()'s use in the shadow-catcher/miss paths),
	// so motion reads as one single (chunkier, but consistent) cadence
	// instead of two mismatched ones.
	void draw(bool hdrToneMapping, bool gammaCorrection, float screenGamma, float iblExposure, int toneMapMode, bool forceOpaque = false);

	bool hasFrame() const { return _hasFrame; }

	// Suppresses draw() until the next upload() - call when a fresh
	// RtPathTracingSession starts, so the previous (now stale - different
	// camera position) frame doesn't flash on screen during the gap between
	// the session starting and its first pass actually publishing.
	void invalidate() { _hasFrame = false; }

private:
	std::unique_ptr<ShaderProgram> _shader;
	GLuint _vao     = 0;
	GLuint _vbo     = 0;
	GLuint _texture = 0;
	int _texWidth   = 0;
	int _texHeight  = 0;
	bool _hasFrame  = false;
};
