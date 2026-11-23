cmake_minimum_required(VERSION 3.18)
project(ParasolidTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
# Stage X1 sources are standard C++ only (no Qt, no OCCT).
set(XT_SOURCES ${REPO}/src/Import/Parasolid/XtLayout.cpp ${REPO}/src/Import/Parasolid/XtReader.cpp
    ${REPO}/src/Import/Parasolid/XtJtLayouts.cpp ${REPO}/src/Import/Parasolid/XtModel.cpp)

add_executable(parasolid_xt_tests xt_reader_tests.cpp ${XT_SOURCES})
target_include_directories(parasolid_xt_tests PRIVATE ${REPO}/include/Import/Parasolid)
enable_testing()
add_test(NAME parasolid_xt_tests COMMAND parasolid_xt_tests)

# Local-sample inspector for owner gate G1 (not a test; needs the untracked layout file).
add_executable(parasolid_xt_decode xt_decode_cli.cpp ${XT_SOURCES})
target_include_directories(parasolid_xt_decode PRIVATE ${REPO}/include/Import/Parasolid)

# Stages X3-X5: OCCT shape construction. Optional: needs OpenCASCADE (pass
# -DCMAKE_PREFIX_PATH=<vcpkg_installed>/x64-windows to find the vcpkg copy).
find_package(OpenCASCADE QUIET)
if(OpenCASCADE_FOUND)
    add_executable(parasolid_xt_shapes xt_shapes_cli.cpp ${XT_SOURCES} ${REPO}/src/Import/Parasolid/XtOcct.cpp ${REPO}/src/Import/Parasolid/XtImport.cpp)
    target_include_directories(parasolid_xt_shapes PRIVATE ${REPO}/include/Import/Parasolid ${REPO}/include/Import ${REPO}/include ${OpenCASCADE_INCLUDE_DIR})
    target_link_libraries(parasolid_xt_shapes PRIVATE TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo TKTopAlgo
                          TKShHealing TKMesh TKPrim TKBO TKOffset)
    add_executable(parasolid_step_summary step_summary_cli.cpp)
    target_include_directories(parasolid_step_summary PRIVATE ${OpenCASCADE_INCLUDE_DIR})
    target_link_libraries(parasolid_step_summary PRIVATE TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo TKTopAlgo
                          TKShHealing TKDESTEP TKXSBase)
else()
    message(STATUS "OpenCASCADE not found: parasolid_xt_shapes is not built")
endif()
