cmake_minimum_required(VERSION 3.18)

project(ModelViewer)

find_package(assimp REQUIRED)

# aiTextureType_ANISOTROPY (KHR_materials_anisotropy) was only added in a
# later Assimp release than some system-package Ubuntu versions ship, and
# Assimp doesn't expose a compile-time version macro to guard on directly -
# probe for the symbol itself instead. See MaterialProcessor.h's use of
# MODELVIEWER_HAVE_ASSIMP_ANISOTROPY.
include(CheckCXXSourceCompiles)
get_target_property(_assimp_include_dirs assimp::assimp INTERFACE_INCLUDE_DIRECTORIES)
set(CMAKE_REQUIRED_INCLUDES ${_assimp_include_dirs})
check_cxx_source_compiles("
#include <assimp/material.h>
int main() { return static_cast<int>(aiTextureType_ANISOTROPY); }
" MODELVIEWER_HAVE_ASSIMP_ANISOTROPY)
unset(CMAKE_REQUIRED_INCLUDES)
if(MODELVIEWER_HAVE_ASSIMP_ANISOTROPY)
    add_compile_definitions(MODELVIEWER_HAVE_ASSIMP_ANISOTROPY)
endif()

find_package(Freetype REQUIRED)
find_package(glm REQUIRED)
find_package(OpenCASCADE REQUIRED)
find_package(meshoptimizer REQUIRED)
# basisu's exported basisu-targets.cmake references zstd::libzstd in
# basisu_encoder's link interface without a find_dependency(zstd) call of its
# own, so that target must already exist by CMake's generate phase. It's
# usually pulled in transitively by Qt6Core's own zstd feature dependency,
# but that's not guaranteed across Qt6 builds/versions - find it explicitly.
find_package(zstd CONFIG REQUIRED)
find_package(basisu CONFIG REQUIRED)
find_package(embree CONFIG REQUIRED)
find_package(OpenImageDenoise CONFIG REQUIRED)
find_package(OpenEXR CONFIG REQUIRED)

message(STATUS "VCPKG_INSTALLED_DIR: ${VCPKG_INSTALLED_DIR}")

if(NOT OpenCASCADE_FOUND)
    message(FATAL_ERROR "Cannot build the executable without OpenCASCADE. Please set the variable OpenCASCADE_DIR.")
else()
    message(STATUS "Found OpenCASCADE " ${OpenCASCADE_VERSION})
endif()

# ============================================================================
# Optional GPU path tracer backend (CUDA + OptiX 7.7) - mirrors the existing
# WebEngine/OIDN-CUDA pattern of graceful degradation: probe for the
# toolchain without hard-failing the configure step when it's absent (most
# dev machines and CI won't have an NVIDIA GPU/CUDA Toolkit/OptiX SDK
# installed), and compile RtOptixContext.cpp as an inert stub in that case -
# see its #ifdef MODELVIEWER_HAVE_OPTIX guard.
# Recent glibc (2.41+, as shipped by rolling-release-ish Linux distros) declares
# rsqrt()/rsqrtf() (and on some CUDA versions cospi()/sinpi()/etc.) itself, with
# a noexcept specifier that conflicts with CUDA's own declaration of the same
# functions in crt/math_functions.h - a hard "exception specification is
# incompatible" compile error the moment any .cu file is compiled, entirely
# unrelated to this project's own code (see e.g. NVIDIA/cuda-samples#403 and
# multiple NVIDIA developer forum threads for the same glibc/CUDA mismatch).
# -U_GNU_SOURCE/-D_DEFAULT_SOURCE steers glibc's headers away from declaring
# those GNU-extension names at all, avoiding the collision. NVCC_APPEND_FLAGS
# is an env var nvcc itself reads on every invocation (including CMake's own
# internal check_language()/try_compile probes, which don't inherit
# CMAKE_CUDA_FLAGS), so it has to be set before check_language(CUDA) below -
# setting CMAKE_CUDA_FLAGS alone would leave that detection probe still
# failing on affected systems even though the real build would've worked.
if(NOT WIN32)
    set(ENV{NVCC_APPEND_FLAGS} "-Xcompiler -U_GNU_SOURCE -Xcompiler -D_DEFAULT_SOURCE")
endif()

include(CheckLanguage)
check_language(CUDA)
set(MODELVIEWER_HAVE_OPTIX FALSE)
if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    find_package(CUDAToolkit QUIET)

    if(NOT WIN32)
        # Belt-and-braces alongside the NVCC_APPEND_FLAGS env var above: bakes
        # the same flags into the generated build rules directly, so the real
        # build doesn't depend on that env var still being set in whatever
        # environment later invokes ninja/make (env var mutations made during
        # this configure don't propagate to a separate later build invocation).
        set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -Xcompiler -U_GNU_SOURCE -Xcompiler -D_DEFAULT_SOURCE")
    endif()

    # OptiX 7.x ships as headers only (optix.h + friends) - the host API is
    # resolved dynamically at runtime from the NVIDIA display driver via
    # optixInit(), so there's no .lib to find/link, just the include path.
    # OPTIX_SDK_VERSION selects which installed SDK version to look for (the
    # NVIDIA installer's standard install location is versioned in the
    # directory name itself, e.g. "OptiX SDK 7.7.0" vs "OptiX SDK 8.0.0") -
    # bump this to pick up a newer SDK on hardware/drivers that support it,
    # without needing to touch the path logic below. OPTIX_ROOT_DIR lets a
    # developer override the install location entirely independent of the
    # version. The default itself is platform-dependent: Windows installs to
    # a ProgramData path, Linux to a versioned directory under /opt/nvidia
    # (the NVIDIA .sh installer's default).
    set(OPTIX_SDK_VERSION "9.1.0" CACHE STRING
        "OptiX SDK version to locate (matches the installer's default directory name, e.g. 7.7.0)")
    if(WIN32)
        set(OPTIX_ROOT_DIR_DEFAULT "C:/ProgramData/NVIDIA Corporation/OptiX SDK ${OPTIX_SDK_VERSION}")
    else()
        set(OPTIX_ROOT_DIR_DEFAULT "/opt/nvidia/NVIDIA-OptiX-SDK-${OPTIX_SDK_VERSION}-linux64-x86_64")
    endif()
    set(OPTIX_ROOT_DIR "${OPTIX_ROOT_DIR_DEFAULT}" CACHE PATH
        "Path to the OptiX SDK install (the directory containing include/optix.h)")
    find_path(OPTIX_INCLUDE_DIR
        NAMES optix.h
        PATHS "${OPTIX_ROOT_DIR}/include"
        NO_DEFAULT_PATH
    )

    # bin2c (bundled with the CUDA Toolkit) turns a compiled .ptx file into a
    # C byte array we embed directly in the executable - see the PTX-embed
    # custom command further down. Avoids needing to ship/locate a loose
    # .ptx file alongside the exe at runtime (the same technique the OptiX
    # SDK samples themselves use, via their own ptx2cpp.cmake).
    find_program(BIN2C_EXECUTABLE bin2c HINTS "${CUDAToolkit_BIN_DIR}")

    if(CUDAToolkit_FOUND AND OPTIX_INCLUDE_DIR AND BIN2C_EXECUTABLE)
        set(MODELVIEWER_HAVE_OPTIX TRUE)
        message(STATUS "OptiX GPU path tracer backend: ENABLED (CUDA ${CUDAToolkit_VERSION}, OptiX ${OPTIX_SDK_VERSION} headers at ${OPTIX_INCLUDE_DIR})")
    else()
        message(STATUS "OptiX GPU path tracer backend: disabled (CUDA Toolkit found=${CUDAToolkit_FOUND}, OptiX ${OPTIX_SDK_VERSION} headers found=${OPTIX_INCLUDE_DIR}, bin2c found=${BIN2C_EXECUTABLE})")
    endif()
else()
    message(STATUS "OptiX GPU path tracer backend: disabled (no CUDA compiler found)")
endif()

find_package(Qt6 COMPONENTS Core Gui OpenGLWidgets Widgets Svg LinguistTools)

# Qt6_FOUND must be captured here, immediately after the call above - a
# second find_package(Qt6 COMPONENTS ...) call (the optional WebEngineWidgets
# probe just below) re-evaluates Qt6_FOUND against ITS OWN requested
# components, clobbering the real result of this one whenever WebEngineWidgets
# isn't available. That previously produced a spurious "Qt6 not found"
# message (and skipped setting QT_TRANSLATE_NAMESPACE) even on fully working
# configures, merely masked because Qt6Config.cmake also sets QT_VERSION_MAJOR
# itself as an unconditional side effect, so the actual QT_VERSION_MAJOR EQUAL
# 6 gate further down in this file kept working anyway.
if(Qt6_FOUND)
    set(QT_VERSION_MAJOR 6)
    set(QT_TRANSLATE_NAMESPACE Qt6)
else()
    message("Qt6 not found")
endif()

message("Using Qt${QT_VERSION_MAJOR}")

# Optional: WebEngine - just check for it, don't link yet
find_package(Qt6 COMPONENTS WebEngineWidgets QUIET)

if(Qt6WebEngineWidgets_FOUND)
    message(STATUS "Qt WebEngine found - tutorials will use WebEngine")
    set(HAVE_WEBENGINE TRUE)
else()
    message(WARNING "Qt WebEngine not found - tutorials will use QTextBrowser")
    set(HAVE_WEBENGINE FALSE)
endif()

# ============================================================================
# Define vcpkg plugins directory (different for debug/release)
# ============================================================================
if(DEFINED VCPKG_INSTALLED_DIR)
    if(DEFINED VCPKG_TARGET_TRIPLET)
        set(VCPKG_BASE "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    else()
        set(VCPKG_BASE "${VCPKG_INSTALLED_DIR}/x64-windows")
    endif()
    
    # For debug builds, plugins are in debug/Qt6/plugins
    # For release builds, plugins are in Qt6/plugins
    if(CMAKE_BUILD_TYPE MATCHES "Debug")
        set(QT_VCPKG_PLUGINS_DIR "${VCPKG_BASE}/debug/Qt6/plugins")
        set(DEBUG_SUFFIX "d")
    else()
        set(QT_VCPKG_PLUGINS_DIR "${VCPKG_BASE}/Qt6/plugins")
        set(DEBUG_SUFFIX "")
    endif()
    
    message(STATUS "VCPKG_INSTALLED_DIR: ${VCPKG_INSTALLED_DIR}")
    message(STATUS "VCPKG_TARGET_TRIPLET: ${VCPKG_TARGET_TRIPLET}")
    message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
    message(STATUS "Qt plugins directory: ${QT_VCPKG_PLUGINS_DIR}")
    
    # Check if directory exists and list plugins
    if(EXISTS "${QT_VCPKG_PLUGINS_DIR}/imageformats")
        message(STATUS "Image format plugins found!")
        file(GLOB IMAGEFORMAT_PLUGINS "${QT_VCPKG_PLUGINS_DIR}/imageformats/*.dll")
        message(STATUS "Available plugins:")
        foreach(PLUGIN ${IMAGEFORMAT_PLUGINS})
            get_filename_component(PLUGIN_NAME ${PLUGIN} NAME)
            message(STATUS "  ${PLUGIN_NAME}")
        endforeach()
    else()
        message(WARNING "Image format plugins directory not found: ${QT_VCPKG_PLUGINS_DIR}/imageformats")
    endif()
else()
    message(WARNING "VCPKG_INSTALLED_DIR not defined")
endif()

# ============================================================================
# Function to deploy image format plugins
#
# Extra arguments (ARGN) name additional runtime DLLs this specific plugin
# depends on that vcpkg's automatic "applocal" DLL deployment never sees -
# that mechanism only scans ModelViewer.exe's own direct import table, but an
# imageformats plugin is a separate binary loaded at runtime via
# QPluginLoader, so a dependency ONLY that plugin needs (e.g. qjpeg.dll's
# dependency on libjpeg-turbo's jpeg62.dll - qjpeg is the one Qt plugin here
# that isn't otherwise linked by anything in this project) is invisible to
# it. These come from vcpkg's own bin/ (not the Qt plugins tree) and are
# copied next to ModelViewer.exe itself (NOT into plugins/imageformats/
# alongside the plugin) - Windows does not reliably resolve a plugin DLL's
# own imports against its own plugin subdirectory, but the app's own
# directory is always on the standard DLL search path.
# ============================================================================
function(deploy_image_plugin TARGET_NAME PLUGIN_NAME)
    if(NOT DEFINED QT_VCPKG_PLUGINS_DIR)
        return()
    endif()

    # Plugin file with appropriate suffix
    set(PLUGIN_FILE "${PLUGIN_NAME}${DEBUG_SUFFIX}.dll")
    set(PLUGIN_SOURCE "${QT_VCPKG_PLUGINS_DIR}/imageformats/${PLUGIN_FILE}")

    if(EXISTS "${PLUGIN_SOURCE}")
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${TARGET_NAME}>/plugins/imageformats"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PLUGIN_SOURCE}"
                "$<TARGET_FILE_DIR:${TARGET_NAME}>/plugins/imageformats/${PLUGIN_FILE}"
            COMMENT "Copying image format plugin: ${PLUGIN_FILE}"
            VERBATIM
        )
        message(STATUS "Will deploy plugin: ${PLUGIN_FILE}")
    else()
        message(WARNING "Plugin not found: ${PLUGIN_SOURCE}")
    endif()

    if(ARGN AND DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        if(CMAKE_BUILD_TYPE MATCHES "Debug")
            set(PLUGIN_DEP_BIN_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin")
        else()
            set(PLUGIN_DEP_BIN_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
        endif()

        foreach(PLUGIN_DEP ${ARGN})
            set(PLUGIN_DEP_SOURCE "${PLUGIN_DEP_BIN_DIR}/${PLUGIN_DEP}")
            if(EXISTS "${PLUGIN_DEP_SOURCE}")
                # Next to the exe (NOT plugins/imageformats/) - Windows
                # doesn't reliably resolve a plugin DLL's own imports against
                # its own directory here, but the app directory is always on
                # the standard DLL search path, so that's where this needs
                # to land.
                add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${PLUGIN_DEP_SOURCE}"
                        "$<TARGET_FILE_DIR:${TARGET_NAME}>/${PLUGIN_DEP}"
                    COMMENT "Copying ${PLUGIN_NAME} dependency: ${PLUGIN_DEP}"
                    VERBATIM
                )
                message(STATUS "Will deploy plugin dependency: ${PLUGIN_DEP}")
            else()
                message(WARNING "Plugin dependency not found: ${PLUGIN_DEP_SOURCE}")
            endif()
        endforeach()
    endif()
endfunction()

# ============================================================================
# Function to deploy the Qt platform plugin (qwindows.dll) - without this,
# the app has NO platform plugin at all in its own build output directory
# and fails to launch outright ("This application failed to start because no
# Qt platform plugin could be initialized") - qt.conf (below) points Qt's
# plugin search path at "../plugins" relative to the exe, but nothing here
# previously copied anything into plugins/platforms/, only plugins/imageformats/.
# This apparently went unnoticed for a long time because some now-removed
# build output happened to already have a platforms/ folder sitting around
# from a much earlier one-off deployment - a full vcpkg-triggered
# reconfigure/clean (as when bumping the vcpkg baseline) wipes that
# untracked leftover, which is what actually surfaced this gap.
# ============================================================================
function(deploy_platform_plugin TARGET_NAME PLUGIN_NAME)
    if(NOT DEFINED QT_VCPKG_PLUGINS_DIR)
        return()
    endif()

    set(PLUGIN_FILE "${PLUGIN_NAME}${DEBUG_SUFFIX}.dll")
    set(PLUGIN_SOURCE "${QT_VCPKG_PLUGINS_DIR}/platforms/${PLUGIN_FILE}")

    if(EXISTS "${PLUGIN_SOURCE}")
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${TARGET_NAME}>/plugins/platforms"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PLUGIN_SOURCE}"
                "$<TARGET_FILE_DIR:${TARGET_NAME}>/plugins/platforms/${PLUGIN_FILE}"
            COMMENT "Copying platform plugin: ${PLUGIN_FILE}"
            VERBATIM
        )
        message(STATUS "Will deploy plugin: ${PLUGIN_FILE}")
    else()
        message(WARNING "Plugin not found: ${PLUGIN_SOURCE}")
    endif()
endfunction()

include(GNUInstallDirs)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Handle UI files manually
file(GLOB ui_sources ui/*.ui)
qt6_wrap_ui(ui_wrapped ${ui_sources})

# ============================================================================
# CREATE THE TARGET - This must come before any target_* commands
# ============================================================================
add_executable(ModelViewer)

set(APP_VERSION_MAJOR 2026)
set(APP_VERSION_MINOR 7)
set(APP_VERSION_PATCH 0)


if(WIN32)
    # Windows: will compute at runtime relative to exe
    set(USE_RELATIVE_DATA_DIR 1)
    set(DATA_DIR_VALUE "${PROJECT_NAME}")
else()
    # Linux: absolute path from make install
    set(USE_RELATIVE_DATA_DIR 0)
    set(DATA_DIR_VALUE "${CMAKE_INSTALL_FULL_DATAROOTDIR}/${PROJECT_NAME}")
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/config.h"
    @ONLY
)

file(GLOB sources src/*.cpp src/*.cxx src/*.c include/*.h include/*.hxx)
file(GLOB ui_sources ui/*.ui)

# Create translations directory if it doesn't exist
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/translations)

set(TS_FILES
    translations/modelviewer_en.ts
    translations/modelviewer_fr.ts
    translations/modelviewer_de.ts
    translations/modelviewer_es.ts
    translations/modelviewer_it.ts
)

# Create empty .ts files if they don't exist
foreach(_ts ${TS_FILES})
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${_ts})
        file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/${_ts}
"<?xml version=\"1.0\" encoding=\"utf-8\"?>
<!DOCTYPE TS>
<TS version=\"2.1\">
</TS>
")
    endif()
endforeach()

# Generate/update translation files using lupdate.
# Qt6's LinguistTools component is requested without REQUIRED above, so on
# platforms/vcpkg builds where it isn't available, Qt6_LUPDATE_EXECUTABLE is
# silently empty - which would collapse the COMMAND below down to just
# "${sources} ${ui_sources} -ts ...", making the shell try to *execute* the
# first source/header file as if it were the lupdate binary. Guard against
# that by only wiring up the real lupdate COMMAND when the executable exists.
if(Qt6_LUPDATE_EXECUTABLE)
    add_custom_target(update_translations
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ModelViewer --parallel 1 || true
        COMMAND ${Qt6_LUPDATE_EXECUTABLE}
            ${sources} ${ui_sources}
            -ts ${TS_FILES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Updating .ts files"
        DEPENDS ${ui_wrapped}
    )
else()
    add_custom_target(update_translations
        COMMAND ${CMAKE_COMMAND} -E echo "Skipping: Qt6 LinguistTools (lupdate) not found"
        COMMENT "Updating .ts files (skipped - lupdate unavailable)"
    )
endif()
    
# Compile .ts files to .qm files
qt6_add_lrelease(ModelViewer
    TS_FILES ${TS_FILES}
    QM_FILES_OUTPUT_VARIABLE QM_FILES
)

# Debug: Print QM files list
message(STATUS "QM_FILES will be: ${QM_FILES}")

# Create a resource file for translations
set(TRANSLATIONS_QRC ${CMAKE_CURRENT_BINARY_DIR}/translations.qrc)
file(WRITE ${TRANSLATIONS_QRC} "<!DOCTYPE RCC>\n<RCC version=\"1.0\">\n<qresource prefix=\"/translations\">\n")

foreach(qm_file ${QM_FILES})
    get_filename_component(qm_name ${qm_file} NAME)
    file(APPEND ${TRANSLATIONS_QRC} "    <file>${qm_file}</file>\n")
endforeach()

file(APPEND ${TRANSLATIONS_QRC} "</qresource>\n</RCC>")

# Add the translations resource to the executable
qt_add_resources(translation_resources ${TRANSLATIONS_QRC})

target_sources(ModelViewer
    PRIVATE
        ${sources}
        ${ui_wrapped}
        ModelViewer.qrc
        ${translation_resources}
)

target_include_directories(ModelViewer
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}
        ${OpenCASCADE_INCLUDE_DIR}
        ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include
)

# ============================================================================
# Set target properties
# ============================================================================
if(WIN32)
    set_target_properties(ModelViewer PROPERTIES WIN32_EXECUTABLE ON)
endif()

# OCCT's STEP transfer recurses deeply through TransferEntity → TransferShape
# chains for assembly files.  In debug builds (no inlining, /RTC1 frame checks)
# the default 1 MB stack is insufficient.  16 MB is safe headroom without waste.
if(MSVC)
    target_link_options(ModelViewer PRIVATE $<$<CONFIG:Debug>:/STACK:16777216>)
endif()

# ============================================================================
# Link libraries - NOW the target exists
# ============================================================================
target_link_libraries(ModelViewer
    PRIVATE
        assimp::assimp
        Freetype::Freetype
        glm::glm
        meshoptimizer::meshoptimizer
        Qt6::Core
        Qt6::Gui
        Qt6::OpenGLWidgets
        Qt6::Widgets
        Qt6::Svg
        basisu::basisu_encoder
        embree
        OpenImageDenoise
        OpenEXR::OpenEXR
        ${OpenCASCADE_FoundationClasses_LIBRARIES}
        ${OpenCASCADE_ModelingData_LIBRARIES}
        ${OpenCASCADE_ModelingAlgorithms_LIBRARIES}
        ${OpenCASCADE_DataExchange_LIBRARIES}
        ${OpenCASCADE_ApplicationFramework_LIBRARIES}
)

# Qt6's automatic static-plugin linking (triggered by linking Qt6::Gui/Widgets
# above) only pulls in the one platform plugin qtbase was configured with as
# its default (xcb here) - a second platform plugin built via an extra vcpkg
# feature (wayland) is compiled into a static lib but never actually
# registered as an available QPA plugin unless explicitly imported. Without
# this, the app only ever offers "xcb" at runtime (confirmed via
# QT_QPA_PLATFORM=wayland failing with "Could not find the Qt platform plugin
# wayland" despite qtwayland having built fine) - it has no effect on Windows,
# which never has more than one platform plugin to begin with.
#
# Only meaningful for a statically-linked Qt (vcpkg's x64-linux triplet):
# a dynamically-linked Qt (e.g. the system apt packages used for the .deb
# build) loads every platform plugin it finds under its plugins/ directory at
# runtime regardless, and qt_import_plugins() itself expects targets like
# Qt6::QXcbIntegrationPlugin to exist as importable static plugin libraries -
# they don't for a shared build, hence the "plug-in ... is not known to the
# current Qt installation" warnings this guard avoids.
get_target_property(_modelviewer_qt_core_type Qt6::Core TYPE)
if(UNIX AND NOT APPLE AND _modelviewer_qt_core_type STREQUAL "STATIC_LIBRARY")
    qt_import_plugins(ModelViewer
        INCLUDE Qt6::QXcbIntegrationPlugin Qt6::QWaylandIntegrationPlugin
    )
endif()

# Add WebEngine if found
if(HAVE_WEBENGINE)
    target_compile_definitions(ModelViewer PRIVATE HAVE_WEBENGINE)
    target_link_libraries(ModelViewer PRIVATE Qt6::WebEngineWidgets)
endif()

# Add the optional GPU (OptiX) path tracer backend if the toolchain was found
# above - see MODELVIEWER_HAVE_OPTIX's doc comment.
if(MODELVIEWER_HAVE_OPTIX)
    target_compile_definitions(ModelViewer PRIVATE MODELVIEWER_HAVE_OPTIX)
    target_include_directories(ModelViewer PRIVATE ${OPTIX_INCLUDE_DIR})
    # CUDA::cuda_driver (the Driver API - cuModuleLoadDataEx()/cuLaunchKernel(),
    # <cuda.h>) is needed alongside CUDA::cudart (the Runtime API - cudaMalloc()/
    # cudaMemcpy(), <cuda_runtime.h>) specifically for RtOptixSkinning's PTX
    # compute kernel below: it's compiled to raw PTX the same way OptiX kernels
    # are (nvcc --ptx + embed, NOT a native CMake CUDA-target build - see
    # add_optix_kernel()'s own doc comment for why), and loading/launching raw
    # PTX at runtime is a Driver API operation regardless of how it was
    # compiled. Both targets share the same CUDA context safely (a standard,
    # well-supported interop pattern) - no separate context/device management
    # needed alongside the existing OptiX/cudart usage elsewhere in this file.
    target_link_libraries(ModelViewer PRIVATE CUDA::cudart CUDA::cuda_driver)

    # OptiX device code (src/cuda/*.cu) can't be compiled as normal CUDA
    # target sources (enable_language(CUDA)'s default flow links a device
    # executable/cubin, which optixTrace()/the SBT/traversal built-ins don't
    # understand) - it needs to become PTX instead, which is what nvcc's
    # --ptx flag produces directly. Each kernel below: nvcc compiles the .cu
    # to .ptx, then EmbedPTX.cmake (our own equivalent of the OptiX SDK's
    # ptx2cpp.cmake) runs bin2c on it and writes a generated .cpp exposing
    # the PTX as a `const char*` string constant - added to the executable's
    # sources like any other .cpp, with zero runtime file dependency.
    function(add_optix_kernel KERNEL_NAME CU_FILE VARIABLE_NAME PARAMS_HEADER)
        set(ptx_file "${CMAKE_CURRENT_BINARY_DIR}/${KERNEL_NAME}.ptx")
        set(embedded_cpp "${CMAKE_CURRENT_BINARY_DIR}/${KERNEL_NAME}_ptx.cpp")

        add_custom_command(
            OUTPUT "${ptx_file}"
            COMMAND ${CMAKE_CUDA_COMPILER}
                --ptx
                # See the glibc/CUDA rsqrt() doc comment above CMAKE_CUDA_FLAGS
                # near this file's CUDA-detection block - this custom command
                # calls nvcc directly rather than through CMake's own CUDA
                # compile rules, so it doesn't pick up CMAKE_CUDA_FLAGS and
                # needs the same fix applied explicitly here too.
                # Single genex list (not four separate ones) + COMMAND_EXPAND_LISTS
                # below: a genex that IS an entire argument and evaluates to ""
                # is still passed to nvcc as a literal empty-string argument
                # rather than being omitted, which on Windows produced
                # `--ptx "" "" "" "" ...` and broke the build ("a single input
                # file is required..."). Expanding a single semicolon-separated
                # list genex drops it entirely when empty.
                "$<$<NOT:$<PLATFORM_ID:Windows>>:-Xcompiler;-U_GNU_SOURCE;-Xcompiler;-D_DEFAULT_SOURCE>"
                -I "${OPTIX_INCLUDE_DIR}"
                -I "${CMAKE_CURRENT_SOURCE_DIR}/include"
                "${CMAKE_CURRENT_SOURCE_DIR}/${CU_FILE}"
                -o "${ptx_file}"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${CU_FILE}" "${CMAKE_CURRENT_SOURCE_DIR}/include/${PARAMS_HEADER}"
            COMMENT "Compiling ${CU_FILE} to PTX"
            COMMAND_EXPAND_LISTS
            VERBATIM
        )

        add_custom_command(
            OUTPUT "${embedded_cpp}"
            COMMAND ${CMAKE_COMMAND}
                -DBIN2C_EXECUTABLE:FILEPATH=${BIN2C_EXECUTABLE}
                -DPTX_FILE:FILEPATH=${ptx_file}
                -DCPP_FILE:FILEPATH=${embedded_cpp}
                -DVARIABLE_NAME:STRING=${VARIABLE_NAME}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedPTX.cmake"
            DEPENDS "${ptx_file}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedPTX.cmake"
            COMMENT "Embedding ${KERNEL_NAME}.ptx as ${VARIABLE_NAME}"
            VERBATIM
        )

        target_sources(ModelViewer PRIVATE "${embedded_cpp}")
    endfunction()

    add_optix_kernel(RtOptixScene src/cuda/RtOptixScene.cu g_rtOptixScenePtx RtOptixSceneParams.h)
    # Not an OptiX pipeline kernel (no raygen/closesthit/miss, no optix.h) -
    # a plain per-vertex compute kernel (GPU skinning) that happens to reuse
    # this same PTX-compile-and-embed machinery because it's the only kernel-
    # code path this project already has - see RtOptixSkinning.cu's own doc
    # comment and RtOptixSceneTracer's Driver-API launch site for how the
    # embedded PTX below actually gets loaded/run (cuModuleLoadDataEx()/
    # cuLaunchKernel(), not optixLaunch()).
    add_optix_kernel(RtOptixSkinning src/cuda/RtOptixSkinning.cu g_rtOptixSkinningPtx RtOptixSkinningParams.h)
    # Same story as RtOptixSkinning above, for GPU morph-target blending
    # instead of skeletal skinning - see RtOptixMorph.cu's own doc comment.
    add_optix_kernel(RtOptixMorph src/cuda/RtOptixMorph.cu g_rtOptixMorphPtx RtOptixMorphParams.h)
endif()


# ============================================================================
# Deploy image format plugins - MUST BE AFTER add_executable()
# ============================================================================
deploy_image_plugin(ModelViewer "qwebp")
deploy_image_plugin(ModelViewer "qjpeg" "jpeg62.dll")
deploy_image_plugin(ModelViewer "qgif")
deploy_image_plugin(ModelViewer "qico")
deploy_image_plugin(ModelViewer "qtiff")
deploy_image_plugin(ModelViewer "qwbmp")
deploy_image_plugin(ModelViewer "qicns")
deploy_image_plugin(ModelViewer "qtga")
# JPEG-2000 (via the "jasper" vcpkg feature just enabled on qtimageformats) -
# plugin/runtime-DLL names are best guesses (jasper's own vcpkg port doesn't
# document its output DLL name) - both calls degrade to a harmless "not
# found" warning if wrong, same as every other deploy_image_plugin() call;
# check the "Available plugins:" listing this file already prints during
# configure and correct the names here if needed.
deploy_image_plugin(ModelViewer "qjp2" "jasper.dll")

# ============================================================================
# Deploy the Qt platform plugin - required for the app to launch at all
# ============================================================================
deploy_platform_plugin(ModelViewer "qwindows")

# ============================================================================
# Deploy WebP libraries and their dependencies (optional, non-fatal) Windows only
# ============================================================================

if(WIN32)
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        if(CMAKE_BUILD_TYPE MATCHES "Debug")
            set(LIBWEBP_BIN_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin")
        else()
            set(LIBWEBP_BIN_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
        endif()
        
    # Check if WebP libraries exist - only deploy if available
        if(EXISTS "${LIBWEBP_BIN_DIR}/libwebp.dll")
            message(STATUS "WebP libraries found in vcpkg, will deploy (Windows only)")
            
        # List of WebP libraries AND their dependencies to deploy
            set(LIBWEBP_DLLS 
                "libwebp.dll"
                "libwebpdecoder.dll"
                "libwebpdemux.dll"
                "libwebpmux.dll"
                "libsharpyuv.dll"
            )
            
            foreach(WEBP_DLL ${LIBWEBP_DLLS})
                set(WEBP_DLL_SOURCE "${LIBWEBP_BIN_DIR}/${WEBP_DLL}")
                
                if(EXISTS "${WEBP_DLL_SOURCE}")
                    # Copy to bin/ directory
                    add_custom_command(TARGET ModelViewer POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${WEBP_DLL_SOURCE}"
                            "$<TARGET_FILE_DIR:ModelViewer>/${WEBP_DLL}"
                        VERBATIM
                    )
                    
                    # Also copy to plugins/imageformats/
                    add_custom_command(TARGET ModelViewer POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E make_directory
                            "$<TARGET_FILE_DIR:ModelViewer>/plugins/imageformats"
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${WEBP_DLL_SOURCE}"
                            "$<TARGET_FILE_DIR:ModelViewer>/plugins/imageformats/${WEBP_DLL}"
                        VERBATIM
                    )
                    
                    message(STATUS "Will deploy: ${WEBP_DLL}")
                else()
                    message(WARNING "WebP library not found (optional): ${WEBP_DLL_SOURCE}")
                endif()
            endforeach()
        else()
        message(STATUS "WebP libraries not found in vcpkg - WebP support will not be available")
        # Non-fatal - build continues without WebP
        endif()
    endif()
else()
    # Linux / macOS: Qt finds plugins and libraries through standard paths
    message(STATUS "Non-Windows platform detected - skipping WebP DLL deployment (not needed)")
endif()

# ============================================================================
# Deploy Embree/TBB/OpenImageDenoise runtime shared libraries - even under the
# vcpkg x64-linux triplet's static linkage (VCPKG_LIBRARY_LINKAGE static),
# OpenImageDenoise and TBB still build as .so (embree itself links statically
# fine, but OIDN's CPU/CUDA device backends are loaded as plugins at runtime -
# same reason the Windows block below deploys them explicitly rather than
# relying on install(TARGETS ... RUNTIME_DEPENDENCIES), which only sees direct
# link-time dependencies). Without this, a Linux install/.deb runs into
# "error while loading shared libraries: libOpenImageDenoise.so.2: cannot open
# shared object file" the moment it's moved out of the build tree, since the
# build tree's RUNPATH points at vcpkg's build-tree lib dir, not anywhere the
# installed binary can rely on.
# ============================================================================
if(UNIX AND NOT APPLE)
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        if(CMAKE_BUILD_TYPE MATCHES "Debug")
            set(RT_LIB_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib")
        else()
            set(RT_LIB_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib")
        endif()

        file(GLOB RAYTRACING_RUNTIME_SOS
            "${RT_LIB_DIR}/libtbb.so*"
            "${RT_LIB_DIR}/libtbbbind*.so*"
            "${RT_LIB_DIR}/libOpenImageDenoise*.so*"
        )
        install(FILES ${RAYTRACING_RUNTIME_SOS}
                DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        )

        # Installed binaries have no RPATH by default - point it at the lib/
        # directory installed alongside bin/ (both siblings of the install
        # prefix) so the dynamic loader finds the .so files above without
        # needing LD_LIBRARY_PATH set. $ORIGIN keeps this relocatable.
        set_target_properties(ModelViewer PROPERTIES
            INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
            BUILD_WITH_INSTALL_RPATH FALSE
        )
    elseif(OpenImageDenoise_DIR)
        # System-packages (.deb) build (see CMakePresets.json's
        # linux_system_* presets): no vcpkg toolchain, so VCPKG_INSTALLED_DIR
        # above is never defined. OpenImageDenoise still has no apt package
        # either way, so it's vendored via the minimal manifest in
        # packaging/linux/vendor instead - OpenImageDenoise_DIR is
        # find_package()'s own standard <prefix>/share/OpenImageDenoise,
        # letting this find the matching lib/ two levels up regardless of
        # exactly where that vendor prefix lives. TBB isn't handled here:
        # embree already resolves it against the system's own libtbb.so (see
        # CMakeLists.txt's CUDA/glibc rsqrt doc comment area for the other
        # half of that story - unrelated, just also near this file's other
        # "why isn't X just from the system" explanations).
        get_filename_component(_oidn_vendor_lib_dir "${OpenImageDenoise_DIR}/../../lib" ABSOLUTE)

        file(GLOB RAYTRACING_RUNTIME_SOS
            "${_oidn_vendor_lib_dir}/libOpenImageDenoise*.so*"
        )
        install(FILES ${RAYTRACING_RUNTIME_SOS}
                DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        )

        set_target_properties(ModelViewer PROPERTIES
            INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
            BUILD_WITH_INSTALL_RPATH FALSE
        )
    endif()
endif()

# ============================================================================
# Deploy Embree/TBB/OpenImageDenoise runtime DLLs (dev-convenience copy next
# to the exe; install(TARGETS ... RUNTIME_DEPENDENCIES) below also picks
# these up for packaged installs) - Windows only
# ============================================================================
if(WIN32)
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        if(CMAKE_BUILD_TYPE MATCHES "Debug")
            set(RT_BIN_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin")
        else()
            set(RT_BIN_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
        endif()

        set(RAYTRACING_RUNTIME_DLLS
            "embree4.dll"
            "tbb12.dll"
            "tbbbind.dll"
            "tbbbind_2_0.dll"
            "tbbbind_2_5.dll"
            "OpenImageDenoise.dll"
            "OpenImageDenoise_core.dll"
            "OpenImageDenoise_device_cpu.dll"
            "OpenImageDenoise_device_cuda.dll"
        )

        foreach(RT_DLL ${RAYTRACING_RUNTIME_DLLS})
            set(RT_DLL_SOURCE "${RT_BIN_DIR}/${RT_DLL}")
            if(EXISTS "${RT_DLL_SOURCE}")
                add_custom_command(TARGET ModelViewer POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${RT_DLL_SOURCE}"
                        "$<TARGET_FILE_DIR:ModelViewer>/${RT_DLL}"
                    VERBATIM
                )
                message(STATUS "Will deploy: ${RT_DLL}")
            else()
                message(WARNING "Ray-tracing runtime DLL not found (optional): ${RT_DLL_SOURCE}")
            endif()
        endforeach()

        # OpenEXR/Imath/Iex/IlmThread DLL filenames include a version suffix
        # (e.g. "OpenEXR-3_3.dll") that depends on exactly which OpenEXR
        # version vcpkg's baseline resolves to - glob-discovered here rather
        # than hardcoded like the ray-tracing DLLs above, since that suffix
        # isn't knowable ahead of an actual vcpkg install.
        file(GLOB OPENEXR_RUNTIME_DLLS
            "${RT_BIN_DIR}/OpenEXR*.dll"
            "${RT_BIN_DIR}/Imath*.dll"
            "${RT_BIN_DIR}/Iex*.dll"
            "${RT_BIN_DIR}/IlmThread*.dll"
            "${RT_BIN_DIR}/deflate*.dll"
        )
        foreach(OPENEXR_DLL ${OPENEXR_RUNTIME_DLLS})
            get_filename_component(OPENEXR_DLL_NAME "${OPENEXR_DLL}" NAME)
            add_custom_command(TARGET ModelViewer POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${OPENEXR_DLL}"
                    "$<TARGET_FILE_DIR:ModelViewer>/${OPENEXR_DLL_NAME}"
                VERBATIM
            )
            message(STATUS "Will deploy: ${OPENEXR_DLL_NAME}")
        endforeach()
    endif()
endif()


# ============================================================================
# Create qt.conf - Windows only
# ============================================================================

if(WIN32)
    add_custom_command(TARGET ModelViewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "[Paths]" > "$<TARGET_FILE_DIR:ModelViewer>/qt.conf"
        COMMAND ${CMAKE_COMMAND} -E echo "Plugins = ../plugins" >> "$<TARGET_FILE_DIR:ModelViewer>/qt.conf"
        COMMENT "Creating qt.conf (Windows only)"
        VERBATIM
    )
endif()

if(WIN32)
    target_sources(ModelViewer
        PRIVATE
            ModelViewer.rc
    )
endif()

install(TARGETS ModelViewer)

# Install qt.conf - Windows only
if(WIN32)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/qt.conf"
            DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
endif()

# Install translation files only if they exist
if(QM_FILES)
    install(FILES ${QM_FILES}
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}/translations"
    )
    message(STATUS "Translation files will be installed to: ${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}/translations")
else()
    message(STATUS "No QM files to install")
endif()

install(DIRECTORY fonts shaders themes test-models data
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}"
)

# textures/envmap/skyboxes/HDRI contains ~65 optional, browsable environment-map
# packs (~1.3GB) in addition to the 3 default presets (studio/outdoor/office.hdr)
# referenced directly by ViewportWidget. textures/materials contains ~129
# optional, browsable PBR material presets (~2.2GB) with no code-level
# defaults. Debug installs (local dev/testing) get the full libraries so you
# can browse/test everything; Release installs (used to build the
# installer/AppImage) ship only a curated minimum of each, to keep package
# size reasonable. CONFIGURATIONS is used rather than a configure-time
# if(CMAKE_BUILD_TYPE...) check because multi-config generators (e.g. Visual
# Studio) only know the configuration at build/install time, not configure time.
install(DIRECTORY textures
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}"
        CONFIGURATIONS Debug
)

# Release installs: rather than relying on install(DIRECTORY)'s REGEX/PATTERN
# EXCLUDE (whose path-matching semantics proved ambiguous/unreliable in
# practice), explicitly install every part of textures/ EXCEPT HDRI and
# materials by name, with no exclude patterns involved at all. The curated
# HDRI/materials subsets are installed individually just below.
install(DIRECTORY textures/khronos
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}/textures"
        CONFIGURATIONS Release
)
install(DIRECTORY textures/patterns
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}/textures"
        CONFIGURATIONS Release
)
install(DIRECTORY textures/envmap/floor
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}/textures/envmap"
        CONFIGURATIONS Release
)
install(DIRECTORY textures/envmap/skyboxes/LDRI
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}/textures/envmap/skyboxes"
        CONFIGURATIONS Release
)

# The 3 loose default .hdr files (not inside a named pack subfolder) are
# referenced directly by ViewportWidget's Studio/Outdoor/Office quick presets,
# so they must ship regardless of which packs are curated below.
install(FILES
            "textures/envmap/skyboxes/HDRI/studio.hdr"
            "textures/envmap/skyboxes/HDRI/outdoor.hdr"
            "textures/envmap/skyboxes/HDRI/office.hdr"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}/textures/envmap/skyboxes/HDRI"
        CONFIGURATIONS Release
)

set(_hdri_included_packs
    "@Default" "Canary_Wharf" "Colorful_Studio" "Dancing_Hall"
    "Evening_Museum_Courtyard" "Footprint_Court" "Frozenwaterfall"
    "Newport_Loft" "Venice_Sunset"
)
foreach(_pack ${_hdri_included_packs})
    install(DIRECTORY "textures/envmap/skyboxes/HDRI/${_pack}"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}/textures/envmap/skyboxes/HDRI"
            CONFIGURATIONS Release
    )
endforeach()

set(_material_included_presets
    "ClearCoat_Metallic_Blue" "Fabric_Denim_02" "Fabric_Fabric050"
    "Fabric_Leather026" "Fabric_Leather030" "Fabric_Leather038"
    "Ground_Cobblestone_Floor_07" "Metal_Aluminium" "Metal_Bronze"
    "Metal_Bronze-1" "Metal_Brushed-Metal" "Metal_Chrome" "Metal_Copper"
    "Metal_Dull_Brass" "Metal_Dull_Copper" "Metal_Gold" "Metal_Iron"
    "Metal_Lead" "Metal_Magnesium" "Metal_Nickel" "Metal_Ornate_Brass"
    "Metal_Pewter" "Metal_Pirate_Gold" "Metal_Pitted_Metal" "Metal_Platinum"
    "Metal_Rose_Gold" "Metal_Rusted_Iron" "Metal_Silver_Dull"
    "Metal_Stainless_Steel" "Metal_Steel" "Metal_Titanium" "Metal_Zinc"
    "Organic_Human_Skin-Freckled" "Organic_Human_Skin1" "Paper_Paper001"
    "Paper_Paper005" "Plastic_ABS_Glossy" "Plastic_ABS_Matte"
    "Plastic_Plastic007" "Plastic_Plastic012a" "Plastic_Plastic016a"
    "Plastic_Scuffed-1" "Rubber_Rubber002" "Rubber_Tire" "Stone_Granite006a"
    "Stone_Marble009" "Stone_Rock_06" "Synthetic_Sponge001" "Wall_Bricks"
    "Wall_Bricks_Plaster" "Wall_Glass_Window" "Wood_Antique-Veneer1"
    "Wood_Bark_Brown_02" "Wood_KitchenWood" "Wood_Plywood" "Wood_Wood051"
)
foreach(_preset ${_material_included_presets})
    install(DIRECTORY "textures/materials/${_preset}"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}/textures/materials"
            CONFIGURATIONS Release
    )
endforeach()

# Install image format plugins deployed during build
install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/plugins"
        DESTINATION "."
        OPTIONAL
)

# Install WebP libraries - Windows only
if(WIN32 AND DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    if(CMAKE_BUILD_TYPE MATCHES "Debug")
        set(LIBWEBP_BIN_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin")
    else()
        set(LIBWEBP_BIN_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
    endif()
    
    install(FILES 
        "${LIBWEBP_BIN_DIR}/libwebp.dll"
        "${LIBWEBP_BIN_DIR}/libwebpdecoder.dll"
        "${LIBWEBP_BIN_DIR}/libwebpdemux.dll"
        "${LIBWEBP_BIN_DIR}/libwebpmux.dll"
        "${LIBWEBP_BIN_DIR}/libsharpyuv.dll"
        DESTINATION "${CMAKE_INSTALL_BINDIR}"
        OPTIONAL
    )
endif()

if(WIN32)
   install(TARGETS ModelViewer
        RUNTIME_DEPENDENCIES
            PRE_EXCLUDE_REGEXES
                [[hvsifiletrust.*]]
                [[pdmutilities.*]]
                [[api-ms-win-.*]]
                [[ext-ms-.*]]
                [[qt6.*\\.dll]]
            POST_EXCLUDE_REGEXES
                [[.*system32.*.dll]]
    )

    # install(TARGETS ... RUNTIME_DEPENDENCIES) only resolves direct PE import
    # dependencies via binary scanning - OpenImageDenoise_device_cpu.dll and
    # OpenImageDenoise_device_cuda.dll (OIDN's CPU/CUDA compute backends,
    # both loaded via LoadLibrary as plugins, not a linker import of
    # OpenImageDenoise.dll/_core.dll) and the tbbbind*.dll NUMA-topology
    # plugins TBB loads the same way are invisible to it, so a packaged
    # install silently ends up without them while the dev build output
    # (which gets them from the POST_BUILD copy above) works fine. That
    # mismatch was the actual cause of OIDN reporting "unsupported device
    # type: CPU" only in installed/packaged builds - the DLL exists, it just
    # was never deployed there. Install them explicitly here too.
    #
    # Same underlying problem for jpeg62.dll/jasper.dll: qjpeg.dll/qjp2.dll
    # are Qt PLUGINS (loaded via QPluginLoader, not linked by ModelViewer.exe
    # itself), so their own dependency on these libs is just as invisible to
    # RUNTIME_DEPENDENCIES' binary scan of ModelViewer.exe. The install(DIRECTORY
    # ... plugins ...) rule below only installs the plugins/ subfolder from
    # the build tree - it does NOT pick up deploy_image_plugin()'s ARGN-extra-
    # dependency copies, which land next to the built exe (build tree root),
    # not inside plugins/imageformats/. Install them explicitly here too, into
    # CMAKE_INSTALL_BINDIR (where ModelViewer.exe itself is installed), for the
    # same "next to the exe, not next to the plugin" reason those copies
    # target the build tree's exe directory rather than plugins/imageformats/.
    if(DEFINED RT_BIN_DIR)
        install(FILES
            "${RT_BIN_DIR}/OpenImageDenoise_device_cpu.dll"
            "${RT_BIN_DIR}/OpenImageDenoise_device_cuda.dll"
            "${RT_BIN_DIR}/tbbbind.dll"
            "${RT_BIN_DIR}/tbbbind_2_0.dll"
            "${RT_BIN_DIR}/tbbbind_2_5.dll"
            "${RT_BIN_DIR}/jpeg62.dll"
            "${RT_BIN_DIR}/jasper.dll"
            DESTINATION "${CMAKE_INSTALL_BINDIR}"
            OPTIONAL
        )
    endif()
endif()

# qt_generate_deploy_app_script is Qt6's own cross-platform deployment
# tooling - it deploys the runtime plugins Qt loads via dlopen at runtime
# (platform plugin like libqxcb.so, imageformats, iconengines, etc.) and
# sets up the rpath/qt.conf so the app finds them relative to itself. This
# matters even where Qt itself is statically linked (e.g. this project's
# vcpkg x64-linux triplet, VCPKG_LIBRARY_LINKAGE static): Qt's plugin
# architecture always loads platform/imageformat plugins as separate
# dynamically-loaded .so/.dll files regardless of how Qt's own core
# libraries were linked, so without this a Linux build has no platform
# plugin at all and can't create a window. Previously gated to WIN32 only,
# which meant Linux installs/AppImages were missing these entirely.
#
# Skipped on Linux when Qt is dynamically linked (the system apt packages
# used for the .deb build): qt6_deploy_runtime_dependencies() there tries to
# bundle the ENTIRE system Qt install's plugin set into our own install tree
# (avahi/cups printing support, embedded-Linux EGL device integration, etc -
# things this app never uses), and can hard-fail on system .so files with no
# RPATH section to rewrite. None of that bundling is needed anyway: a
# dynamically-linked Qt already finds its plugins at the system's standard
# Qt plugin path, which the target machine has via its own qt6-base/
# qt6-image-formats-plugins packages (declared in the .deb's Depends:).
if(QT_VERSION_MAJOR EQUAL 6 AND (WIN32 OR _modelviewer_qt_core_type STREQUAL "STATIC_LIBRARY"))
    qt_generate_deploy_app_script(
        TARGET ModelViewer
        OUTPUT_SCRIPT deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
        NO_COMPILER_RUNTIME
    )

    install(SCRIPT ${deploy_script})
endif()

# ============================================================================
# Linux desktop integration (.desktop file + icon) and .deb packaging (CPack)
# ============================================================================
if(UNIX AND NOT APPLE)
    install(FILES ModelViewer.desktop
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications"
            RENAME modelviewer.desktop
    )

    # The repo only ships a Windows .ico - convert its largest frame to a PNG
    # for the Linux hicolor icon theme (freedesktop.org menus/taskbars don't
    # read .ico). ImageMagick's "convert" is already an apt dependency in CI.
    find_program(IMAGEMAGICK_CONVERT convert)
    if(IMAGEMAGICK_CONVERT)
        set(MODELVIEWER_PNG_ICON "${CMAKE_CURRENT_BINARY_DIR}/modelviewer.png")
        add_custom_command(
            OUTPUT "${MODELVIEWER_PNG_ICON}"
            COMMAND "${IMAGEMAGICK_CONVERT}" "${CMAKE_CURRENT_SOURCE_DIR}/res/ModelViewer.ico[0]" "${MODELVIEWER_PNG_ICON}"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/res/ModelViewer.ico"
            COMMENT "Converting ModelViewer.ico to PNG for the Linux .desktop icon"
        )
        add_custom_target(modelviewer_icon_png ALL DEPENDS "${MODELVIEWER_PNG_ICON}")
        install(FILES "${MODELVIEWER_PNG_ICON}"
                DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/256x256/apps"
                RENAME modelviewer.png
        )
    else()
        message(WARNING "ImageMagick 'convert' not found - the Linux .desktop icon will not be installed")
    endif()

    # CPack DEB generator: packages whatever the install() rules above produce
    # (this project links Qt/OpenCASCADE/etc. statically via the vcpkg
    # x64-linux triplet, so there's no separate Qt runtime to bundle the way
    # the old AppImage packaging needed to - see the CI workflow's history for
    # that approach). CPACK_DEBIAN_PACKAGE_SHLIBDEPS runs dpkg-shlibdeps over
    # every installed binary (the exe and Qt's dynamically-loaded plugins
    # alike) to compute the real runtime dependency list (libc, libstdc++,
    # libX11/libxcb/libGL, etc.) instead of hand-maintaining one here.
    set(CPACK_GENERATOR "DEB")
    set(CPACK_PACKAGE_NAME "modelviewer")
    set(CPACK_PACKAGE_VERSION "${APP_VERSION_MAJOR}.${APP_VERSION_MINOR}.${APP_VERSION_PATCH}")
    set(CPACK_PACKAGE_VENDOR "Sharjith N")
    set(CPACK_PACKAGE_CONTACT "Sharjith N <sharjith@gmail.com>")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "3D model viewer for CAD/mesh formats with PBR and path-traced rendering")
    set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/sharjith/ModelViewer-Qt")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/gpl-3.0.txt")

    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
    set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")

    include(CPack)
endif()
