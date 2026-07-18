cmake_minimum_required(VERSION 3.18)

project(ModelViewer)

find_package(assimp REQUIRED)
find_package(Freetype REQUIRED)
find_package(glm REQUIRED)
find_package(OpenCASCADE REQUIRED)
find_package(meshoptimizer REQUIRED)
find_package(basisu CONFIG REQUIRED)
find_package(embree 3 CONFIG REQUIRED)
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
include(CheckLanguage)
check_language(CUDA)
set(MODELVIEWER_HAVE_OPTIX FALSE)
if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    find_package(CUDAToolkit QUIET)

    # OptiX 7.x ships as headers only (optix.h + friends) - the host API is
    # resolved dynamically at runtime from the NVIDIA display driver via
    # optixInit(), so there's no .lib to find/link, just the include path.
    # OPTIX_SDK_VERSION selects which installed SDK version to look for (the
    # NVIDIA installer's standard Windows location is versioned in the
    # directory name itself, e.g. "OptiX SDK 7.7.0" vs "OptiX SDK 8.0.0") -
    # bump this to pick up a newer SDK on hardware/drivers that support it,
    # without needing to touch the path logic below. OPTIX_ROOT_DIR lets a
    # developer override the install location entirely (e.g. a non-default
    # install path, or Linux) independent of the version.
    set(OPTIX_SDK_VERSION "9.1.0" CACHE STRING
        "OptiX SDK version to locate (matches the installer's default directory name, e.g. 7.7.0)")
    set(OPTIX_ROOT_DIR "C:/ProgramData/NVIDIA Corporation/OptiX SDK ${OPTIX_SDK_VERSION}" CACHE PATH
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

find_package(Qt6 COMPONENTS Core Gui OpenGLWidgets Widgets LinguistTools)

# Optional: WebEngine - just check for it, don't link yet
find_package(Qt6 COMPONENTS WebEngineWidgets QUIET)

if(Qt6WebEngineWidgets_FOUND)
    message(STATUS "Qt WebEngine found - tutorials will use WebEngine")
    set(HAVE_WEBENGINE TRUE)
else()
    message(WARNING "Qt WebEngine not found - tutorials will use QTextBrowser")
    set(HAVE_WEBENGINE FALSE)
endif()

if(Qt6_FOUND)
    set(QT_VERSION_MAJOR 6)
    set(QT_TRANSLATE_NAMESPACE Qt6)
else()
    message("Qt6 not found")
endif()

message("Using Qt${QT_VERSION_MAJOR}")

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
    target_link_libraries(ModelViewer PRIVATE CUDA::cudart)

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
                -I "${OPTIX_INCLUDE_DIR}"
                -I "${CMAKE_CURRENT_SOURCE_DIR}/include"
                "${CMAKE_CURRENT_SOURCE_DIR}/${CU_FILE}"
                -o "${ptx_file}"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${CU_FILE}" "${CMAKE_CURRENT_SOURCE_DIR}/include/${PARAMS_HEADER}"
            COMMENT "Compiling ${CU_FILE} to PTX"
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

    add_optix_kernel(RtOptixTriangle src/cuda/RtOptixTriangle.cu g_rtOptixTrianglePtx RtOptixTriangleParams.h)
    add_optix_kernel(RtOptixScene src/cuda/RtOptixScene.cu g_rtOptixScenePtx RtOptixSceneParams.h)
endif()


# ============================================================================
# Deploy image format plugins - MUST BE AFTER add_executable()
# ============================================================================
deploy_image_plugin(ModelViewer "qwebp")
deploy_image_plugin(ModelViewer "qjpeg")
deploy_image_plugin(ModelViewer "qgif")
deploy_image_plugin(ModelViewer "qico")
deploy_image_plugin(ModelViewer "qtiff")
deploy_image_plugin(ModelViewer "qwbmp")

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
            "embree3.dll"
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
    if(DEFINED RT_BIN_DIR)
        install(FILES
            "${RT_BIN_DIR}/OpenImageDenoise_device_cpu.dll"
            "${RT_BIN_DIR}/OpenImageDenoise_device_cuda.dll"
            "${RT_BIN_DIR}/tbbbind.dll"
            "${RT_BIN_DIR}/tbbbind_2_0.dll"
            "${RT_BIN_DIR}/tbbbind_2_5.dll"
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
if(QT_VERSION_MAJOR EQUAL 6)
    qt_generate_deploy_app_script(
        TARGET ModelViewer
        OUTPUT_SCRIPT deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
        NO_COMPILER_RUNTIME
    )

    install(SCRIPT ${deploy_script})
endif()
