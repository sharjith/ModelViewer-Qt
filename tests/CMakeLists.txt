# GUI-free unit tests. Enabled with -DMV_BUILD_TESTS=ON (see the root CMakeLists.txt). The sources under
# test depend only on QtCore + the standard library, so they are compiled directly into the test
# executable instead of linking the whole application.
add_executable(result_tests
    result_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/Simulation/Core/ResultDataset.cpp
    ${CMAKE_SOURCE_DIR}/src/Simulation/Readers/ResultReader.cpp
    ${CMAKE_SOURCE_DIR}/src/Simulation/Readers/VtkXmlReader.cpp
    ${CMAKE_SOURCE_DIR}/src/Simulation/Readers/VtkLegacyReader.cpp
    ${CMAKE_SOURCE_DIR}/src/Simulation/Readers/CalculixFrdReader.cpp
    ${CMAKE_SOURCE_DIR}/src/Simulation/Core/ResultDerivedFields.cpp
    ${CMAKE_SOURCE_DIR}/src/Simulation/Core/ResultUnits.cpp
    ${CMAKE_SOURCE_DIR}/src/Simulation/Core/ResultBoundary.cpp
    ${CMAKE_SOURCE_DIR}/src/Simulation/Core/ComparePaneLayout.cpp
    ${CMAKE_SOURCE_DIR}/src/Simulation/Readers/OpenFoamReader.cpp
    ${CMAKE_SOURCE_DIR}/src/Simulation/Readers/ExodusReader.cpp
    ${CMAKE_SOURCE_DIR}/src/Simulation/Readers/CgnsReader.cpp
    ${CMAKE_SOURCE_DIR}/src/Simulation/Readers/VtkHdfReader.cpp
    ${CMAKE_SOURCE_DIR}/src/Simulation/Readers/MedReader.cpp
    ${CMAKE_SOURCE_DIR}/src/Simulation/Core/ResultSnapshot.cpp
    ${CMAKE_SOURCE_DIR}/src/Simulation/Core/SimulationResultDisplay.cpp
    ${CMAKE_SOURCE_DIR}/src/Simulation/Core/SimulationGlyphs.cpp
    ${CMAKE_SOURCE_DIR}/src/Simulation/Core/ResultSlice.cpp
    ${CMAKE_SOURCE_DIR}/src/Simulation/Core/ResultStreamlines.cpp
    ${CMAKE_SOURCE_DIR}/src/Simulation/Core/SimulationCharts.cpp
    ${CMAKE_SOURCE_DIR}/src/Simulation/Core/SimulationVolume.cpp
)
target_include_directories(result_tests PRIVATE ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/Simulation/Core ${CMAKE_SOURCE_DIR}/include/Simulation/Readers ${CMAKE_SOURCE_DIR}/include/Simulation/UI)
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
# VTKHDF needs the HDF5 C library; without it the reader is a stub and its tests are skipped.
find_package(HDF5 CONFIG QUIET COMPONENTS C)
if(HDF5_FOUND AND (TARGET hdf5::hdf5-shared OR TARGET hdf5::hdf5-static))
    target_link_libraries(result_tests PRIVATE $<IF:$<TARGET_EXISTS:hdf5::hdf5-shared>,hdf5::hdf5-shared,hdf5::hdf5-static>)
    target_compile_definitions(result_tests PRIVATE MV_HAVE_HDF5=1)
endif()
set_target_properties(result_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

enable_testing()
add_test(NAME result_tests COMMAND result_tests)

# General-purpose 3-D plotting is deliberately independent of Simulation/ResultDataset. Keep its core parser/model
# in a small target so regressions are quick to run and cannot acquire an accidental GUI dependency.
add_executable(plot3d_tests
    plot3d_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/Plot3D/Core/Plot3DData.cpp
	${CMAKE_SOURCE_DIR}/src/Plot3D/Core/Plot3DFormula.cpp
    ${CMAKE_SOURCE_DIR}/src/Plot3D/Core/Plot3DMeshBuilder.cpp
    ${CMAKE_SOURCE_DIR}/src/Plot3D/Core/Plot3DSessionIO.cpp
    ${CMAKE_SOURCE_DIR}/src/Plot3D/UI/Plot3DAxisController.cpp
)
target_include_directories(plot3d_tests PRIVATE ${CMAKE_SOURCE_DIR}/include/Plot3D/Core ${CMAKE_SOURCE_DIR}/include/Plot3D/UI)
target_link_libraries(plot3d_tests PRIVATE Qt6::Core Qt6::Gui CGAL::CGAL)
set_target_properties(plot3d_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
add_test(NAME plot3d_tests COMMAND plot3d_tests)
