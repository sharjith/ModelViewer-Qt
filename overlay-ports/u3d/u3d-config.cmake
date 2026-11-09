# find_package(u3d CONFIG REQUIRED)
#
# Provides:
#   u3d::idtf             static library + include directories + definitions for the in-process libIDTF API
#   U3D_MODULE_NAMES      the run-time modules IFXCOMInitialize() loads (without prefix/suffix)
#   U3D_MODULE_DIR_RELEASE / U3D_MODULE_DIR_DEBUG   folders that contain them
#   U3D_MODULE_PREFIX / U3D_MODULE_SUFFIX           file name pieces ("" ".dll" on Windows, "lib" ".so" on Linux)
# The modules must be deployed next to the application executable (flat layout), see the port's usage file.
get_filename_component(_u3d_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

if(NOT TARGET u3d::idtf)
    add_library(u3d::idtf STATIC IMPORTED)
    set(_u3d_inc "${_u3d_root}/include/u3d")
    set_target_properties(u3d::idtf PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES
            "${_u3d_inc}/RTL/Component/Include;${_u3d_inc}/RTL/Kernel/Include;${_u3d_inc}/RTL/Platform/Include;${_u3d_inc}/RTL/Component/Base;${_u3d_inc}/RTL/Dependencies/WildCards;${_u3d_inc}/IDTF;${_u3d_inc}/IDTF/Include;${_u3d_inc}/IDTF/Common;${_u3d_inc}/libIDTF")
    if(WIN32)
        set_target_properties(u3d::idtf PROPERTIES
            IMPORTED_LOCATION_RELEASE "${_u3d_root}/lib/IDTF.lib"
            IMPORTED_LOCATION_DEBUG "${_u3d_root}/debug/lib/IDTF.lib"
            IMPORTED_CONFIGURATIONS "RELEASE;DEBUG"
            INTERFACE_LINK_LIBRARIES "winmm"
            INTERFACE_COMPILE_DEFINITIONS "U3D_NO_ASM;_CRT_SECURE_NO_DEPRECATE")
        set(U3D_MODULE_PREFIX "")
        set(U3D_MODULE_SUFFIX ".dll")
        set(U3D_MODULE_DIR_RELEASE "${_u3d_root}/bin")
        set(U3D_MODULE_DIR_DEBUG "${_u3d_root}/debug/bin")
    else()
        set_target_properties(u3d::idtf PROPERTIES
            IMPORTED_LOCATION_RELEASE "${_u3d_root}/lib/libIDTF.a"
            IMPORTED_LOCATION_DEBUG "${_u3d_root}/debug/lib/libIDTF.a"
            IMPORTED_CONFIGURATIONS "RELEASE;DEBUG"
            INTERFACE_LINK_LIBRARIES "${CMAKE_DL_LIBS}"
            INTERFACE_COMPILE_DEFINITIONS "U3D_NO_ASM;LIN32")
        set(U3D_MODULE_PREFIX "lib")
        set(U3D_MODULE_SUFFIX ".so")
        set(U3D_MODULE_DIR_RELEASE "${_u3d_root}/lib/u3d-modules")
        set(U3D_MODULE_DIR_DEBUG "${_u3d_root}/debug/lib/u3d-modules")
    endif()
    set(U3D_MODULE_NAMES IFXCore IFXExporting IFXImporting IFXScheduling)
endif()
set(u3d_FOUND TRUE)
