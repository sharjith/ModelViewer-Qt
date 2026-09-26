# GUI-free unit tests. Enabled with -DMV_BUILD_TESTS=ON (see the root CMakeLists.txt). The sources under
# test depend only on QtCore + the standard library, so they are compiled directly into the test
# executable instead of linking the whole application.
add_executable(result_tests
    result_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/ResultDataset.cpp
    ${CMAKE_SOURCE_DIR}/src/ResultReader.cpp
    ${CMAKE_SOURCE_DIR}/src/VtkXmlReader.cpp
    ${CMAKE_SOURCE_DIR}/src/VtkLegacyReader.cpp
    ${CMAKE_SOURCE_DIR}/src/CalculixFrdReader.cpp
    ${CMAKE_SOURCE_DIR}/src/ResultDerivedFields.cpp
    ${CMAKE_SOURCE_DIR}/src/ResultUnits.cpp
    ${CMAKE_SOURCE_DIR}/src/ResultBoundary.cpp
    ${CMAKE_SOURCE_DIR}/src/ComparePaneLayout.cpp
    ${CMAKE_SOURCE_DIR}/src/OpenFoamReader.cpp
    ${CMAKE_SOURCE_DIR}/src/ExodusReader.cpp
    ${CMAKE_SOURCE_DIR}/src/CgnsReader.cpp
    ${CMAKE_SOURCE_DIR}/src/ResultSnapshot.cpp
    ${CMAKE_SOURCE_DIR}/src/SimulationResultDisplay.cpp
    ${CMAKE_SOURCE_DIR}/src/SimulationGlyphs.cpp
)
target_include_directories(result_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
# The real-file tests read the sample results shipped with the project.
target_compile_definitions(result_tests PRIVATE
    MV_SIMULATION_SAMPLES_DIR="${CMAKE_SOURCE_DIR}/sample-models/Simulation")
target_link_libraries(result_tests PRIVATE Qt6::Core)
# Exodus II needs NetCDF; without it the reader is a stub and its tests are skipped.
find_package(netCDF CONFIG QUIET)
if(netCDF_FOUND)
    target_link_libraries(result_tests PRIVATE netCDF::netcdf)
    target_compile_definitions(result_tests PRIVATE MV_HAVE_NETCDF=1)
endif()
# CGNS likewise; without the library its reader is a stub and its tests are skipped.
find_package(cgns CONFIG QUIET)
if(cgns_FOUND)
    target_link_libraries(result_tests PRIVATE $<IF:$<TARGET_EXISTS:CGNS::cgns_shared>,CGNS::cgns_shared,CGNS::cgns_static>)
    target_compile_definitions(result_tests PRIVATE MV_HAVE_CGNS=1)
endif()
set_target_properties(result_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

enable_testing()
add_test(NAME result_tests COMMAND result_tests)
