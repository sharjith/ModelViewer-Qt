cmake_minimum_required(VERSION 3.18)
project(MaterialRasterTests LANGUAGES CXX)
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL)
find_package(glm CONFIG REQUIRED)
get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_executable(material_wireframe_tests material_wireframe_tests.cpp)
target_sources(material_wireframe_tests PRIVATE ${REPO}/src/Materials/Material.cpp)
target_include_directories(material_wireframe_tests PRIVATE ${REPO}/include/Render ${REPO}/include/Materials)
target_link_libraries(material_wireframe_tests PRIVATE Qt6::Gui Qt6::OpenGL glm::glm)
target_compile_features(material_wireframe_tests PRIVATE cxx_std_17)
enable_testing()
add_test(NAME material_wireframe_tests COMMAND material_wireframe_tests)
set_tests_properties(material_wireframe_tests PROPERTIES SKIP_RETURN_CODE 77)

option(MV_RT_VERTEX_ALPHA_TESTS "Build the CPU/Embree vertex-alpha regression" OFF)
if(MV_RT_VERTEX_ALPHA_TESTS)
  find_package(embree CONFIG REQUIRED)
  find_package(Qt6 REQUIRED COMPONENTS Widgets)
  find_package(Threads REQUIRED)
  # PathUtils needs the same config vocabulary as the application. Keep this
  # isolated test's configuration in its own generated build directory.
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/rt-alpha-config")
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/rt-alpha-config/config.h"
    "#pragma once\n#define USE_RELATIVE_DATA_DIR 1\n#define MODELVIEWER_DATA_DIR \"ModelViewer\"\n")
  add_executable(rt_vertex_alpha_tests rt_vertex_alpha_tests.cpp
    ${REPO}/src/RayTracing/CpuPathTracer.cpp
    ${REPO}/src/RayTracing/RtEmbreeScene.cpp
    ${REPO}/src/RayTracing/RtEnvironmentSampler.cpp)
  target_include_directories(rt_vertex_alpha_tests PRIVATE ${REPO}/include/RayTracing
    ${REPO}/include/Core "${CMAKE_CURRENT_BINARY_DIR}/rt-alpha-config")
  target_link_libraries(rt_vertex_alpha_tests PRIVATE embree Qt6::Widgets glm::glm Threads::Threads)
  target_compile_features(rt_vertex_alpha_tests PRIVATE cxx_std_17)
  add_test(NAME rt_vertex_alpha_tests COMMAND rt_vertex_alpha_tests)
endif()

# Pure sampler regression: exercises the same lookup used by CpuPathTracer.
# No Embree, CUDA, window or OpenGL context is needed by this target.
add_executable(rt_texture_filter_tests rt_texture_filter_tests.cpp)
target_include_directories(rt_texture_filter_tests PRIVATE ${REPO}/include/RayTracing)
target_link_libraries(rt_texture_filter_tests PRIVATE glm::glm)
target_compile_features(rt_texture_filter_tests PRIVATE cxx_std_17)
add_test(NAME rt_texture_filter_tests COMMAND rt_texture_filter_tests)

add_executable(blend_depth_policy_tests blend_depth_policy_tests.cpp)
target_include_directories(blend_depth_policy_tests PRIVATE ${REPO}/include/Render)
target_link_libraries(blend_depth_policy_tests PRIVATE Qt6::Gui Qt6::OpenGL)
target_compile_definitions(blend_depth_policy_tests PRIVATE MV_REPO_DIR="${REPO}")
target_compile_features(blend_depth_policy_tests PRIVATE cxx_std_17)
add_test(NAME blend_depth_policy_tests COMMAND blend_depth_policy_tests)
set_tests_properties(blend_depth_policy_tests PROPERTIES SKIP_RETURN_CODE 77)
