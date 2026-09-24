# GUI-free unit tests. Enabled with -DMV_BUILD_TESTS=ON (see the root CMakeLists.txt). The sources under
# test depend only on QtCore + the standard library, so they are compiled directly into the test
# executable instead of linking the whole application.
add_executable(result_tests
    result_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/ResultDataset.cpp
    ${CMAKE_SOURCE_DIR}/src/ResultReader.cpp
    ${CMAKE_SOURCE_DIR}/src/VtkXmlReader.cpp
    ${CMAKE_SOURCE_DIR}/src/ResultBoundary.cpp
)
target_include_directories(result_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(result_tests PRIVATE Qt6::Core)
set_target_properties(result_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

enable_testing()
add_test(NAME result_tests COMMAND result_tests)
