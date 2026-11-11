# GUI-free unit tests. Enabled with -DMV_BUILD_TESTS=ON (see the root CMakeLists.txt). The sources under
# test depend only on QtCore + the standard library, so they are compiled directly into the test
# executable instead of linking the whole application.
add_executable(import_trace_tests import_trace_tests.cpp ${CMAKE_SOURCE_DIR}/src/Import/ImportDiagnostics.cpp)
target_include_directories(import_trace_tests PRIVATE ${CMAKE_SOURCE_DIR}/include/Import)
target_link_libraries(import_trace_tests PRIVATE Qt6::Core)
target_compile_features(import_trace_tests PRIVATE cxx_std_17)
add_test(NAME import_trace_tests COMMAND import_trace_tests)

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
    ${CMAKE_SOURCE_DIR}/src/Simulation/Core/PlaybackClock.cpp
    ${CMAKE_SOURCE_DIR}/src/Simulation/Core/SimulationSurfaceLocator.cpp
    ${CMAKE_SOURCE_DIR}/src/Simulation/Core/SimulationCellAveraging.cpp
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
    ${CMAKE_SOURCE_DIR}/src/Plot3D/Core/Plot3DSection.cpp
    ${CMAKE_SOURCE_DIR}/src/Plot3D/Core/Plot3DPathlines.cpp
    ${CMAKE_SOURCE_DIR}/src/Plot3D/Core/Plot3DGenerate.cpp
    ${CMAKE_SOURCE_DIR}/src/Plot3D/UI/Plot3DAxisController.cpp
)
target_include_directories(plot3d_tests PRIVATE ${CMAKE_SOURCE_DIR}/include/Plot3D/Core ${CMAKE_SOURCE_DIR}/include/Plot3D/UI)
target_link_libraries(plot3d_tests PRIVATE Qt6::Core Qt6::Gui CGAL::CGAL)
set_target_properties(plot3d_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
add_test(NAME plot3d_tests COMMAND plot3d_tests)

# MVF loader rules that need no GL context (header-only, QtCore only).
add_executable(mvf_tests mvf_tests.cpp)
target_include_directories(mvf_tests PRIVATE ${CMAKE_SOURCE_DIR}/include/Mvf)
target_link_libraries(mvf_tests PRIVATE Qt6::Core)
set_target_properties(mvf_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
add_test(NAME mvf_tests COMMAND mvf_tests)

# Save as 3D PDF (src/Pdf3D): view dictionaries, aiScene conversion, the PDF incremental update on a real QPdfWriter page and the in-process
# u3d conversion. Needs the Qt offscreen platform plugin; the u3d modules are copied next to the executable so the conversion tests run.
add_executable(pdf3d_tests
    pdf3d_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/Pdf3D/U3dScene.cpp
    ${CMAKE_SOURCE_DIR}/src/Pdf3D/U3dWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/Pdf3D/U3dWorker.cpp
    ${CMAKE_SOURCE_DIR}/src/Pdf3D/SceneToU3d.cpp
    ${CMAKE_SOURCE_DIR}/src/Pdf3D/PdfAppender.cpp
    ${CMAKE_SOURCE_DIR}/src/Pdf3D/Pdf3DViews.cpp
    ${CMAKE_SOURCE_DIR}/src/Pdf3D/OverlayToU3d.cpp
    ${CMAKE_SOURCE_DIR}/src/Pdf3D/Pdf3DExporter.cpp
    ${CMAKE_SOURCE_DIR}/src/Pdf3D/SheetTemplate.cpp
    ${CMAKE_SOURCE_DIR}/src/Pdf3D/SheetRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/Pdf3D/PartData.cpp
    ${CMAKE_SOURCE_DIR}/src/Print/PrintPage.cpp
    ${CMAKE_SOURCE_DIR}/src/Print/PrintPreset.cpp
)
target_include_directories(pdf3d_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include/Pdf3D ${CMAKE_SOURCE_DIR}/include/Scene ${CMAKE_SOURCE_DIR}/include/Print
    ${CMAKE_SOURCE_DIR}/include/Measurement ${CMAKE_SOURCE_DIR}/include/Import)
target_link_libraries(pdf3d_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Svg assimp::assimp u3d::idtf)
set_target_properties(pdf3d_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
foreach(U3D_MODULE ${U3D_MODULE_NAMES})
    set(U3D_MODULE_FILE "${U3D_MODULE_PREFIX}${U3D_MODULE}${U3D_MODULE_SUFFIX}")
    add_custom_command(TARGET pdf3d_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${U3D_MODULE_DIR_DEBUG},${U3D_MODULE_DIR_RELEASE}>/${U3D_MODULE_FILE}"
            "$<TARGET_FILE_DIR:pdf3d_tests>/${U3D_MODULE_FILE}"
        VERBATIM
    )
endforeach()
if(UNIX AND NOT APPLE)
    set_property(TARGET pdf3d_tests APPEND PROPERTY BUILD_RPATH "$ORIGIN")
endif()
if(WIN32)
    # QGuiApplication needs a platform plugin; Qt looks in <exe dir>/platforms. (vcpkg's Qt has no offscreen plugin.)
    add_custom_command(TARGET pdf3d_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pdf3d_tests>/platforms"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<IF:$<CONFIG:Debug>,debug/Qt6/plugins/platforms/qwindowsd.dll,Qt6/plugins/platforms/qwindows.dll>"
            "$<TARGET_FILE_DIR:pdf3d_tests>/platforms/$<IF:$<CONFIG:Debug>,qwindowsd.dll,qwindows.dll>"
        VERBATIM
    )
endif()
add_test(NAME pdf3d_tests COMMAND pdf3d_tests)

# Hierarchical exploded view: group tree builders and the level-by-level layout (QtCore + QtGui, no GL).
add_executable(exploded_view_tests
    exploded_view_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/Scene/ExplodedGroupTree.cpp
    ${CMAKE_SOURCE_DIR}/src/Scene/ExplodedViewManager.cpp
)
target_include_directories(exploded_view_tests PRIVATE ${CMAKE_SOURCE_DIR}/include/Scene)
target_link_libraries(exploded_view_tests PRIVATE Qt6::Core Qt6::Gui)
set_target_properties(exploded_view_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
add_test(NAME exploded_view_tests COMMAND exploded_view_tests)

# Evidence tool (not a test): the materials OpenCASCADE's STEP reader finds in a STEP file. Build on request: ninja step_material_probe
add_executable(step_material_probe step/step_material_probe.cpp)
target_link_libraries(step_material_probe PRIVATE
    ${OpenCASCADE_FoundationClasses_LIBRARIES}
    ${OpenCASCADE_ModelingData_LIBRARIES}
    ${OpenCASCADE_DataExchange_LIBRARIES}
    ${OpenCASCADE_ApplicationFramework_LIBRARIES})
set_target_properties(step_material_probe PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

# Native 3DXML codec and scene parity checks (QtCore, Assimp and ZIP; no GL).
add_subdirectory(3dxml)
add_subdirectory(jt)
