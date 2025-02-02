
cmake_minimum_required(VERSION 3.18)

project(ModelViewer)

find_package(assimp REQUIRED)
find_package(Freetype REQUIRED)
find_package(Qt5 REQUIRED COMPONENTS Gui OpenGL)

add_executable(ModelViewer)

file(GLOB sources *.cpp *.h)
file(GLOB ui_sources *.ui)

qt5_wrap_ui(ui_wrapped ${ui_sources})
qt5_add_resources(resources ModelViewer.qrc)

target_sources(ModelViewer
    PRIVATE
        ${sources}
        ${ui_wrapped}
        ${resources}
)

target_include_directories(ModelViewer
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(ModelViewer
    PRIVATE
        assimp::assimp
        Freetype::Freetype
        Qt5::Gui
        Qt5::OpenGL
)

set_target_properties(ModelViewer PROPERTIES AUTOMOC TRUE)
