cmake_minimum_required(VERSION 3.22)
project(InventorTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# The Inventor readers need Qt Core, zlib, Zstandard and OpenCASCADE (all modules, as the main build uses: the modules refer to
# each other's targets). Without them the test is not built.
find_package(Qt6 QUIET COMPONENTS Core)
find_package(ZLIB QUIET)
find_package(zstd CONFIG QUIET)
find_package(OpenCASCADE QUIET)

if(Qt6_FOUND AND ZLIB_FOUND AND zstd_FOUND AND OpenCASCADE_FOUND)
    add_executable(inventor_tests
        inventor_tests.cpp
        ${REPO}/src/Import/Inventor/Assembly.cpp
        ${REPO}/src/Import/Inventor/CompoundFile.cpp
        ${REPO}/src/Import/Inventor/PartSegment.cpp
        ${REPO}/src/Import/Inventor/SegmentData.cpp
        ${REPO}/src/Import/Inventor/PartSolid.cpp
    )
    target_compile_features(inventor_tests PRIVATE cxx_std_17)
    # SegmentData.h is internal to the importer, so the tests reach it through the source folder.
    target_include_directories(inventor_tests PRIVATE ${REPO}/include/Import ${REPO}/src/Import/Inventor ${OpenCASCADE_INCLUDE_DIR})
    target_compile_definitions(inventor_tests PRIVATE MV_REPO_DIR="${REPO}")
    target_link_libraries(inventor_tests PRIVATE Qt6::Core ZLIB::ZLIB zstd::libzstd
        ${OpenCASCADE_FoundationClasses_LIBRARIES}
        ${OpenCASCADE_ModelingData_LIBRARIES}
        ${OpenCASCADE_ModelingAlgorithms_LIBRARIES}
    )
    enable_testing()
    # Synthetic tests always run; the corpus tests compare local samples with corpus_manifest.tsv when the map file
    # (out/inventor-tests/corpus_map.tsv, or MV_INVENTOR_CORPUS_MAP) exists and skip otherwise.
    add_test(NAME inventor_tests COMMAND inventor_tests)
    # Every part read serially and again with threaded face building forced on; the results must agree (skips when no map).
    add_test(NAME inventor_thread_check COMMAND inventor_tests --thread-check)
else()
    message(STATUS "Qt6 Core, zlib, zstd or OpenCASCADE not found: inventor_tests is not built")
endif()
