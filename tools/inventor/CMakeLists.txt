cmake_minimum_required(VERSION 3.22)
project(InventorCompoundProbe LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(ZLIB REQUIRED)
find_package(zstd CONFIG REQUIRED)
find_package(OpenCASCADE REQUIRED COMPONENTS FoundationClasses ModelingData ModelingAlgorithms)

add_executable(inventor_compound_probe
    compound_probe.cpp
    ../../src/Import/Inventor/CompoundFile.cpp
    ../../src/Import/Inventor/PartSegment.cpp
    ../../src/Import/Inventor/PartSolid.cpp
)
target_compile_features(inventor_compound_probe PRIVATE cxx_std_17)
target_include_directories(inventor_compound_probe PRIVATE ../../include/Import ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(inventor_compound_probe PRIVATE Qt6::Core ZLIB::ZLIB zstd::libzstd
    ${OpenCASCADE_FoundationClasses_LIBRARIES}
    ${OpenCASCADE_ModelingData_LIBRARIES}
    ${OpenCASCADE_ModelingAlgorithms_LIBRARIES}
)
