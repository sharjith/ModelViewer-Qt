#pragma once

// Declares the PTX byte-array symbols generated at build time by
// CMakeLists.txt's add_optix_kernel()/EmbedPTX.cmake (nvcc --ptx + bin2c) -
// see RtOptixScene.cu for the kernel this compiles from. Only declared/used
// when MODELVIEWER_HAVE_OPTIX is defined (the generated .cpp defining this
// symbol is only added to the build in that case too).
extern const char* const g_rtOptixScenePtx;

// Same PTX-embed mechanism, but NOT an OptiX pipeline kernel - a plain
// per-vertex GPU-skinning compute kernel (see src/cuda/RtOptixSkinning.cu),
// loaded/launched via the CUDA Driver API (cuModuleLoadDataEx()/
// cuLaunchKernel()) rather than optixLaunch() - see RtOptixSceneTracer's
// skin-base cache doc comment for the consumer.
extern const char* const g_rtOptixSkinningPtx;

// Same story as g_rtOptixSkinningPtx above, for GPU morph-target blending
// (see src/cuda/RtOptixMorph.cu) - see RtOptixSceneTracer's persistent
// morph-base cache doc comment for the consumer.
extern const char* const g_rtOptixMorphPtx;
