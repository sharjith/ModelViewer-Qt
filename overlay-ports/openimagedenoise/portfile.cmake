# OIDN is not in the upstream vcpkg registry (confirmed against vcpkg master,
# 2026-07-05) - Intel/RenderKit only ship prebuilt binary redistributables on
# GitHub Releases, no source vcpkg port exists. This overlay port repackages
# those binaries. CPU + CUDA devices only: the release archive also bundles
# HIP/SYCL device plugins and their oneAPI Level Zero loader DLLs, neither of
# which this project uses, so those are left out. The CUDA device plugin
# (OpenImageDenoise_device_cuda.dll) is a prebuilt binary already inside this
# same archive - a DLL import scan confirms it links only against
# nvcuda.dll (the NVIDIA display driver's own Driver API library, present on
# any machine with a working NVIDIA GPU driver installed), NOT against any
# CUDA Toolkit redistributable - so no CUDA Toolkit is required to build OR
# run this, only an NVIDIA driver at runtime for the CUDA device to actually
# initialize (RtDenoiser falls back to the CPU device, then to a built-in
# bilateral filter, if it doesn't).
vcpkg_check_linkage(ONLY_DYNAMIC_LIBRARY)

set(OIDN_VERSION "2.5.0")

if(VCPKG_TARGET_IS_WINDOWS)
    set(OIDN_ARCHIVE "oidn-${OIDN_VERSION}.x64.windows.zip")
    set(OIDN_SHA512 "d91c7638c246c0fbc6931d031361e427a801797f659ed0a879340a2f9f8d1795d1b7ea2abd9a2498e9fe6a1a1f4728e98f9ff65ef03da4198c24de6a2bf2ed52")
elseif(VCPKG_TARGET_IS_LINUX)
    set(OIDN_ARCHIVE "oidn-${OIDN_VERSION}.x86_64.linux.tar.gz")
    set(OIDN_SHA512 "3bfa802593c5cc6958e49ebcdc5b937d0cfa07ea245f22307f0e2ef43da61bb41e33d05e69d250a814f50ced322bc36369c09da3addd69870ff08a9854c3f5aa")
else()
    message(FATAL_ERROR "openimagedenoise overlay port: only Windows x64 and Linux x64 prebuilt binaries are wired up (this project's supported triplets).")
endif()

vcpkg_download_distfile(ARCHIVE_PATH
    URLS "https://github.com/RenderKit/oidn/releases/download/v${OIDN_VERSION}/${OIDN_ARCHIVE}"
    FILENAME "${OIDN_ARCHIVE}"
    SHA512 "${OIDN_SHA512}"
)

vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE_PATH}")

# Headers and CMake config are identical for release/debug - OIDN only ships
# release binaries, so debug configs reuse the same runtime files below.
# vcpkg_cmake_config_fixup() requires the debug-side config dir to exist too,
# so the release-side lib/cmake tree is installed into both locations.
file(INSTALL "${SOURCE_PATH}/include/OpenImageDenoise" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
file(INSTALL "${SOURCE_PATH}/lib/cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
file(INSTALL "${SOURCE_PATH}/lib/cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib")

if(VCPKG_TARGET_IS_WINDOWS)
    file(INSTALL
            "${SOURCE_PATH}/lib/OpenImageDenoise.lib"
            "${SOURCE_PATH}/lib/OpenImageDenoise_core.lib"
         DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
    file(INSTALL
            "${SOURCE_PATH}/lib/OpenImageDenoise.lib"
            "${SOURCE_PATH}/lib/OpenImageDenoise_core.lib"
         DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib")

    set(OIDN_RUNTIME_FILES
        "${SOURCE_PATH}/bin/OpenImageDenoise.dll"
        "${SOURCE_PATH}/bin/OpenImageDenoise_core.dll"
        "${SOURCE_PATH}/bin/OpenImageDenoise_device_cpu.dll"
        "${SOURCE_PATH}/bin/OpenImageDenoise_device_cuda.dll"
        "${SOURCE_PATH}/bin/tbb12.dll"
        "${SOURCE_PATH}/bin/tbbbind.dll"
        "${SOURCE_PATH}/bin/tbbbind_2_0.dll"
        "${SOURCE_PATH}/bin/tbbbind_2_5.dll"
    )
    file(INSTALL ${OIDN_RUNTIME_FILES} DESTINATION "${CURRENT_PACKAGES_DIR}/bin")
    file(INSTALL ${OIDN_RUNTIME_FILES} DESTINATION "${CURRENT_PACKAGES_DIR}/debug/bin")
else()
    # GLOB (not a hardcoded list) for the CUDA .so specifically - not verified
    # to exist in the Linux release archive the way it was for Windows above,
    # so this degrades to "not installed" rather than a hard configure error
    # if a given release doesn't ship it for Linux.
    file(GLOB OIDN_SO_FILES
        "${SOURCE_PATH}/lib/libOpenImageDenoise.so*"
        "${SOURCE_PATH}/lib/libOpenImageDenoise_core.so*"
        "${SOURCE_PATH}/lib/libOpenImageDenoise_device_cpu.so*"
        "${SOURCE_PATH}/lib/libOpenImageDenoise_device_cuda.so*"
        "${SOURCE_PATH}/lib/libtbb.so*"
        "${SOURCE_PATH}/lib/libtbbbind.so*"
        "${SOURCE_PATH}/lib/libtbbbind_2_0.so*"
        "${SOURCE_PATH}/lib/libtbbbind_2_5.so*"
    )
    file(INSTALL ${OIDN_SO_FILES} DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
    file(INSTALL ${OIDN_SO_FILES} DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib")
endif()

vcpkg_cmake_config_fixup(PACKAGE_NAME OpenImageDenoise CONFIG_PATH "lib/cmake/OpenImageDenoise-${OIDN_VERSION}")

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/doc/LICENSE.txt")
