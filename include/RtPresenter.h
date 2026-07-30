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
// Presents the path tracer's latest frame (RtRayTracingSession::
// latestFrame()) into the existing OpenGL viewport: uploads the linear HDR
// RGB buffer plus primary-hit alpha into a GL_RGBA32F texture (same upload pattern as
// SceneRenderController's transmission buffer) and draws it as a fullscreen
// triangle, reusing the project's existing fullscreen_triangle.vert plus a
// new small tonemap+gamma fragment shader (ray_traced_present.frag).
//
// Deliberately kept separate from SceneRenderController rather than folded
// into that already-large class - ray tracing is a bolt-on display mode
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
	// top of image - matching RtRayTracingSession/QImage convention) and an
	// optional alpha mask as the frame to present next. Alpha is 0 where the
	// primary ray missed geometry, letting the already-rendered raster
	// gradient/skybox remain visible.
	void upload(const std::vector<glm::vec3>& rgb, int width, int height,
		const std::vector<float>* alpha = nullptr);

	// GPU-resident variant of upload(), for the interactive/CUDA-GL-interop
	// presentation path (see RtOptixRayTracingSession::latestDeviceFrame()'s
	// doc comment) - deviceRgba is a CUDA device pointer already holding
	// width*height linear-HDR float4 RGBA (.w = alpha, same layout upload()
	// packs on the CPU side). Copies it straight into the display texture via
	// a CUDA-registered Pixel Buffer Object (cudaGraphicsGLRegisterBuffer +
	// one device-to-device cudaMemcpy), with no host round-trip at all -
	// eliminating the readback that otherwise dominates interactive-session
	// per-chunk latency. Lazily creates/registers the PBO on first call (and
	// re-registers it if width/height changed, mirroring upload()'s own
	// _texWidth/_texHeight reallocate-on-mismatch pattern); must be called
	// with a current GL context. Returns false - doing nothing to the
	// currently-displayed frame - if CUDA-GL interop isn't available at all
	// (no CUDA/OptiX toolchain in this build) or a driver-level registration/
	// copy ever fails (logged once, then this method keeps returning false
	// for the rest of this RtPresenter's lifetime rather than retrying every
	// frame); callers should fall back to the CPU-vector upload() path in
	// that case, not skip presenting a frame entirely.
	bool uploadFromDevice(void* deviceRgba, int width, int height);

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
	// was configured to show) keeps a ray-traced-vs-raster comparison an
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
	// alpha entirely). ViewportWidget always passes true for the interactive
	// PT path, regardless of whether a skybox is enabled: that path bakes its
	// own background into every launch (see RtOptixScene.cu's
	// sampleEnvironmentBackground()), so it never depends on the separately-
	// timed raster layer underneath. A conditional version of this used to
	// keep normal alpha blending whenever a skybox was enabled, to keep a
	// live raster skybox visible while interactive PT's own background
	// sampling was still incomplete - but with interactive PT rendering its
	// own background every frame, blending against raster instead just let
	// that independently-updating raster layer show through PT's own
	// imperfect/noisy alpha, which read as the background visibly "bleeding
	// through" the model. Unconditional opaque avoids compositing two
	// separately-produced images at all.
	void draw(bool hdrToneMapping, bool gammaCorrection, float screenGamma, float iblExposure, int toneMapMode, bool forceOpaque = false);

	bool hasFrame() const { return _hasFrame; }

	// Suppresses draw() until the next upload() - call when a fresh
	// RtRayTracingSession starts, so the previous (now stale - different
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

	// CUDA-GL interop state for uploadFromDevice() - see its doc comment.
	// _interopCudaResource is a cudaGraphicsResource_t, kept as void* here so
	// this header doesn't need a CUDA include (mirrors RtOptixSceneTracer.h's
	// same void*-in-header pattern for its own device-pointer-facing API).
	GLuint _interopPbo = 0;
	int _interopPboWidth  = 0;
	int _interopPboHeight = 0;
	void* _interopCudaResource = nullptr;
	bool _interopUnavailable = false; // sticky once true - see uploadFromDevice()'s doc comment
};
