cmake_minimum_required(VERSION 3.18)
project(ThreeDXmlTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(assimp REQUIRED)
find_package(minizip CONFIG QUIET)
if(TARGET MINIZIP::minizip)
    set(ZIP_TARGET MINIZIP::minizip)
elseif(TARGET MINIZIP::minizipstatic)
    set(ZIP_TARGET MINIZIP::minizipstatic)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MINIZIP REQUIRED IMPORTED_TARGET minizip)
    set(ZIP_TARGET PkgConfig::MINIZIP)
endif()
get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_executable(threedxml_tests threedxml_tests.cpp
    ${REPO}/src/Import/ThreeDXml/ArchiveReader.cpp
    ${REPO}/src/Import/ThreeDXml/ProductStructureReader.cpp
    ${REPO}/src/Import/ThreeDXml/RepresentationReader.cpp
    ${REPO}/src/Import/ThreeDXml/SceneBuilder.cpp
    ${REPO}/src/Import/ThreeDXml/Importer.cpp)
target_include_directories(threedxml_tests PRIVATE ${REPO}/include/Import/ThreeDXml)
target_link_libraries(threedxml_tests PRIVATE Qt6::Core assimp::assimp ${ZIP_TARGET})
enable_testing()
add_test(NAME threedxml_tests COMMAND threedxml_tests)
