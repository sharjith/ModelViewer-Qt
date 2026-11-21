cmake_minimum_required(VERSION 3.22)
project(InventorCompoundProbe LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(ZLIB REQUIRED)
find_package(zstd CONFIG REQUIRED)
# All modules, as the main build does: the modules reference each other's targets (DataExchange needs Visualization).
find_package(OpenCASCADE REQUIRED)

add_executable(inventor_compound_probe
    compound_probe.cpp
    ../../src/Import/Inventor/Assembly.cpp
    ../../src/Import/Inventor/CompoundFile.cpp
    ../../src/Import/Inventor/PartSegment.cpp
    ../../src/Import/Inventor/SegmentData.cpp
    ../../src/Import/Inventor/PartSolid.cpp
)
target_compile_features(inventor_compound_probe PRIVATE cxx_std_17)
target_include_directories(inventor_compound_probe PRIVATE ../../include/Import ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(inventor_compound_probe PRIVATE Qt6::Core ZLIB::ZLIB zstd::libzstd
    ${OpenCASCADE_FoundationClasses_LIBRARIES}
    ${OpenCASCADE_ModelingData_LIBRARIES}
    ${OpenCASCADE_ModelingAlgorithms_LIBRARIES}
)

# Product tree of an Inventor STEP assembly (names, local and composed transforms) as JSON: the reference for qualifying the
# placement of nested occurrences read from the .iam of the same assembly.
add_executable(step_assembly_reference step_assembly_reference.cpp)
target_compile_features(step_assembly_reference PRIVATE cxx_std_17)
target_include_directories(step_assembly_reference PRIVATE ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(step_assembly_reference PRIVATE Qt6::Core
    ${OpenCASCADE_FoundationClasses_LIBRARIES}
    ${OpenCASCADE_ModelingData_LIBRARIES}
    ${OpenCASCADE_ModelingAlgorithms_LIBRARIES}
    ${OpenCASCADE_ApplicationFramework_LIBRARIES}
    ${OpenCASCADE_DataExchange_LIBRARIES}
)

# Every shell of a STEP file with its bounding box: finds the bodies an Inventor part's own STEP export leaves out.
add_executable(step_shell_boxes step_shell_boxes.cpp)
target_compile_features(step_shell_boxes PRIVATE cxx_std_17)
target_include_directories(step_shell_boxes PRIVATE ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(step_shell_boxes PRIVATE Qt6::Core
    ${OpenCASCADE_FoundationClasses_LIBRARIES}
    ${OpenCASCADE_ModelingData_LIBRARIES}
    ${OpenCASCADE_ModelingAlgorithms_LIBRARIES}
    ${OpenCASCADE_ApplicationFramework_LIBRARIES}
    ${OpenCASCADE_DataExchange_LIBRARIES}
)
