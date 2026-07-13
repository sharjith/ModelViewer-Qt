#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "RtSceneSnapshot.h"

// ---------------------------------------------------------------------------
// RtEnvironmentSampler
//
// Luminance-weighted importance sampler over the scene's environment cubemap
// (RtEnvironment::faces), used for next-event-estimation direct sampling of
// the environment as a "light" (bright HDRI sun/window/sky) - see
// CpuPathTracer.h's doc comment on why this was previously flagged as
// follow-up work: without it, a bright but small/sharp environment feature
// only ever gets lit by BSDF-sampled bounces stumbling into it by chance,
// which converges extremely slowly.
//
// Built once per scene/environment change (RtPathTracingSession rebuilds it
// alongside the Embree BVH, gated on the same "geometry actually changed"
// condition - camera-only changes rebuild neither), then read-only for the
// lifetime of that build - sample()/pdf() are const and safe to call
// concurrently from every render-pass worker thread.
//
// Implementation: a single flat cumulative distribution over every texel of
// every face, weighted by luminance * exact texel solid angle (the standard
// AreaElement()-based cubemap texel solid angle formula, matching what
// tools like AMD's CubeMapGen use for prefiltering). Sampled via one binary
// search per call - simpler than a per-face marginal/conditional 2D CDF
// (the usual approach for a single equirect texture) and works uniformly
// across all 6 faces without needing a separate face-selection step.
// ---------------------------------------------------------------------------
class RtEnvironmentSampler
{
public:
	void build(const RtEnvironment& environment);

	bool isValid() const { return _totalWeight > 0.0f; }

	// Importance-samples a world-space direction proportional to the
	// environment's luminance. u0 picks a texel (via the flat CDF), u1/u2
	// jitter continuously within that texel so repeated samples don't all
	// land on exact texel centers. outPdf is the solid-angle PDF at the
	// returned direction (matches pdf() evaluated there).
	void sample(float u0, float u1, float u2, glm::vec3& outDir, float& outPdf) const;

	// Solid-angle PDF of this distribution at an arbitrary direction -
	// needed to MIS-weight ordinary BSDF-sampled rays that happen to escape
	// to the environment (balance heuristic between this and the BSDF's own
	// sampling pdf).
	float pdf(const glm::vec3& direction) const;

	// Raw distribution data, for uploading to a GPU backend so it can
	// importance-sample/evaluate the SAME distribution CPU built here
	// (RtOptixSceneTracer::buildScene() builds and uploads one of these
	// alongside the environment cubemap) rather than duplicating the CDF-
	// construction algorithm device-side and risking the two engines
	// drifting apart. See this class's own field doc comments for layout.
	int faceSize() const { return _size; }
	float totalWeight() const { return _totalWeight; }
	const std::vector<float>& flatCdf() const { return _flatCdf; }
	const std::vector<float>& texelPdf() const { return _texelPdf; }

private:
	int _size = 0;
	std::vector<float> _flatCdf;   // size 6*_size*_size+1, cumulative, [0..totalWeight]
	std::vector<float> _texelPdf;  // size 6*_size*_size - luminance/_totalWeight per texel (solid-angle density)
	float _totalWeight = 0.0f;
};
