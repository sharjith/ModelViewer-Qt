#pragma once

#include <string>

// ---------------------------------------------------------------------------
// HdrImageLoader
//
// Reads a linear HDR image (Radiance .hdr or OpenEXR .exr) into a flat RGB
// float buffer - the exact layout/allocator convention stb_image's own
// stbi_loadf() uses, so every existing call site (ViewportWidget::
// setSkyBoxTextureFolder()'s per-face loop, loadCubemapFromSingleHDR(),
// SceneRenderController::convertEquirectToCubemap()) can swap to this
// wrapper and keep using stbi_image_free() for cleanup unchanged - the
// returned buffer is always allocated via stb_image's own STBI_MALLOC
// (plain malloc() by default), including for the EXR case, specifically so
// that's true.
//
// Dispatches purely by file extension (case-insensitive ".exr" vs anything
// else, which goes to stb_image as before) - this app had no prior EXR
// support anywhere, so there's no existing behavior to preserve for that
// extension.
// ---------------------------------------------------------------------------
namespace HdrImageLoader
{
	// Mirrors stbi_loadf()'s own signature/contract exactly: returns nullptr
	// on failure, otherwise a malloc'd buffer of outWidth*outHeight*outChannels
	// floats (row-major, channels interleaved) the caller frees with
	// stbi_image_free(). outChannels is always 3 (RGB, alpha discarded) for
	// the EXR path, matching what every current caller already assumes for
	// .hdr files (only ever consumed as GL_RGB/GL_RGB32F uploads).
	//
	// flipVertically matches stbi_set_flip_vertically_on_load()'s effect -
	// each call site already toggles that flag differently depending on
	// which way up ITS particular usage expects the data, so this takes the
	// same decision as an explicit parameter rather than a hidden global,
	// applying it uniformly to whichever backend actually reads the file.
	float* load(const std::string& path, int& outWidth, int& outHeight, int& outChannels, bool flipVertically);

	// True if the given path's extension is one this loader recognizes
	// beyond stb_image's own formats - i.e. ".exr" - so callers building
	// file-type filters/format lists can extend them without hardcoding the
	// extension string themselves in more than one place.
	bool isExr(const std::string& path);
}
