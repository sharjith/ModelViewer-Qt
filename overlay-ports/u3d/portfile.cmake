# u3d (Intel Universal 3D reference code, fork DmytroSytnyk/u3d at a pinned commit). No upstream vcpkg port exists.
#
# What this overlay port installs (everything custom: the upstream install rules are not used):
#   include/u3d/<source dir>/*.h     the headers a client of the in-process libIDTF API needs
#   lib/IDTF.lib | lib/libIDTF.a     static IDTF library (scene classes, converters and the IFXCore stub loader)
#   <modules>                        IFXCore, IFXExporting, IFXImporting, IFXScheduling: run-time modules that
#                                    IFXCOMInitialize() loads from the application's own folder:
#                                      Windows  bin/*.dll               (copy next to ModelViewer.exe)
#                                      Linux    lib/u3d-modules/lib*.so (copy next to the executable, RUNPATH $ORIGIN)
#   share/u3d/u3d-config.cmake       find_package(u3d CONFIG) -> target u3d::idtf and the U3D_MODULE_* variables
#
# Patches (patches/0001-*.patch), see research/3dpdf/3D_PDF_EXPORT_RESEARCH.md section 9.10:
#   - IDTF is always built as a static library (it was shared on non-MSVC) so the application's RUNPATH governs the
#     dlopen() calls of the module loader;
#   - on Linux the IFX module directory defaults to the directory of the running executable.
# The application must never call IFXSetDefaultLocale().

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO DmytroSytnyk/u3d
    REF 5c141d9f0d366357e2b7cf93af2eade284a334be
    SHA512 79c9a8bffc7e8bbe0384b4225c0e7763c14999a8d51291be5e1824ad6e447a93f43eb8b43a702f5877306f78141c074ec8ddc17ba6125f38d80db592ee0236ad
    HEAD_REF master
    PATCHES
        patches/0001-linux-static-idtf-and-module-dir.patch
)

if(VCPKG_TARGET_IS_LINUX)
    # the sources use dynamic exception specifications (removed in C++17) and are old-style C++
    string(APPEND VCPKG_CXX_FLAGS " -std=gnu++14 -fpermissive -w")
    string(APPEND VCPKG_C_FLAGS " -w")
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DU3D_SHARED=OFF                    # bundled jpeg/png/zlib instead of system libraries
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5  # the project declares cmake_minimum_required(2.8)
)
vcpkg_cmake_build()

# ---- headers ---------------------------------------------------------------------------------------------------------
set(U3D_HEADER_DIRS
    RTL/Component/Include RTL/Kernel/Include RTL/Platform/Include RTL/Component/Base
    RTL/Dependencies/WildCards IDTF IDTF/Include IDTF/Common libIDTF)
foreach(dir IN LISTS U3D_HEADER_DIRS)
    file(GLOB headers "${SOURCE_PATH}/${dir}/*.h")
    file(INSTALL ${headers} DESTINATION "${CURRENT_PACKAGES_DIR}/include/u3d/${dir}")
endforeach()

# ---- libraries and modules -------------------------------------------------------------------------------------------
if(VCPKG_TARGET_IS_WINDOWS)
    set(MODULE_SUBDIR "bin")
else()
    set(MODULE_SUBDIR "lib/u3d-modules")
endif()

foreach(config IN ITEMS rel dbg)
    set(build_dir "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-${config}")
    if(config STREQUAL "rel")
        set(prefix "${CURRENT_PACKAGES_DIR}")
    else()
        set(prefix "${CURRENT_PACKAGES_DIR}/debug")
    endif()

    if(VCPKG_TARGET_IS_WINDOWS)
        file(INSTALL "${build_dir}/IDTF.lib" DESTINATION "${prefix}/lib")
        foreach(module IN ITEMS IFXCore IFXExporting IFXImporting IFXScheduling)
            file(INSTALL "${build_dir}/${module}.dll" DESTINATION "${prefix}/${MODULE_SUBDIR}")
        endforeach()
    else()
        file(INSTALL "${build_dir}/libIDTF.a" DESTINATION "${prefix}/lib")
        foreach(module IN ITEMS IFXCore IFXExporting IFXImporting IFXScheduling)
            file(INSTALL "${build_dir}/lib${module}.so" DESTINATION "${prefix}/${MODULE_SUBDIR}")
        endforeach()
    endif()
endforeach()

# ---- CMake package config --------------------------------------------------------------------------------------------
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/u3d-config.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/u3d")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/u3d")

# The static library also contains the bundled zlib, libpng and libjpeg code paths of the build; their licences are noted
# in CREDITS.md of the application. The u3d code itself is Apache-2.0.
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
