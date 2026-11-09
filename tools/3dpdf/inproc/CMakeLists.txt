# Phase 0 spike (throwaway): build inproc_u3d against a u3d checkout and its already built static IDTF library.
#
#   cmake -S tools/3dpdf/inproc -B out/3dpdf/build/inproc -DU3D_SRC=out/3dpdf/src/u3d -DU3D_BUILD=out/3dpdf/build/u3d
#   cmake --build out/3dpdf/build/inproc --config Release
cmake_minimum_required(VERSION 3.20)
project(inproc_u3d CXX)
set(U3D_SRC "" CACHE PATH "u3d source checkout")
set(U3D_BUILD "" CACHE PATH "u3d build tree containing IDTF.lib / libIDTF")
add_executable(inproc_u3d inproc_u3d.cpp)
target_compile_features(inproc_u3d PRIVATE cxx_std_14)
target_compile_definitions(inproc_u3d PRIVATE U3D_NO_ASM)
if(MSVC)
  target_compile_definitions(inproc_u3d PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_DEPRECATE)
else()
  target_compile_definitions(inproc_u3d PRIVATE LIN32)
  target_compile_options(inproc_u3d PRIVATE -fpermissive -w)
endif()
target_include_directories(inproc_u3d PRIVATE
  ${U3D_SRC}/RTL/Component/Include ${U3D_SRC}/RTL/Kernel/Include ${U3D_SRC}/RTL/Platform/Include
  ${U3D_SRC}/RTL/Dependencies/WildCards ${U3D_SRC}/RTL/Component/Base
  ${U3D_SRC}/IDTF ${U3D_SRC}/IDTF/Include ${U3D_SRC}/IDTF/Common ${U3D_SRC}/libIDTF)
find_library(IDTF_LIB NAMES libIDTF.a IDTF PATHS ${U3D_BUILD} ${U3D_BUILD}/Release NO_DEFAULT_PATH REQUIRED)
target_link_libraries(inproc_u3d PRIVATE ${IDTF_LIB} ${CMAKE_DL_LIBS})
if(WIN32)
  target_link_libraries(inproc_u3d PRIVATE winmm)
endif()
if(UNIX)
  # modules (libIFXCore.so and the plugins) are found next to the executable, no LD_LIBRARY_PATH / U3D_LIBDIR needed
  set_target_properties(inproc_u3d PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN" BUILD_WITH_INSTALL_RPATH ON)
endif()
