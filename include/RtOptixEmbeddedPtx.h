#pragma once

// Declares the PTX byte-array symbols generated at build time by
// CMakeLists.txt's add_optix_kernel()/EmbedPTX.cmake (nvcc --ptx + bin2c) -
// see RtOptixTriangle.cu for the kernel this specific one compiles from.
// Only declared/used when MODELVIEWER_HAVE_OPTIX is defined (the generated
// .cpp defining these symbols is only added to the build in that case too).
extern const char* const g_rtOptixTrianglePtx;
extern const char* const g_rtOptixScenePtx;
