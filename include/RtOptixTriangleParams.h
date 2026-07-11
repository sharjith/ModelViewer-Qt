#pragma once

#include <optix_types.h> // OptixTraversableHandle

// ---------------------------------------------------------------------------
// RtOptixTriangleParams
//
// Shared between RtOptixTracer.cpp (host) and src/cuda/RtOptixTriangle.cu
// (device) - the __constant__ launch parameters block for the Phase 1b test
// render. Deliberately as small as optixTriangle's own Params: one hardcoded
// triangle, one camera, one output buffer. No scene snapshot, no materials,
// no bounces - this only exists to prove optixTrace()/closest-hit/miss and
// the CUDA<->host readback path work, before any real rendering is built on
// top of it. float3/uchar4 are CUDA's own built-in vector types (from
// vector_types.h, included transitively via cuda_runtime.h on the host side
// and automatically in .cu device compilation) - safe to name in a header
// included from both sides.
// ---------------------------------------------------------------------------
struct RtOptixTriangleParams
{
	uchar4* image;
	unsigned int imageWidth;
	unsigned int imageHeight;

	float3 camEye;
	float3 camU;
	float3 camV;
	float3 camW;

	OptixTraversableHandle handle;
};
