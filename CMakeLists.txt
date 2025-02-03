
cmake_minimum_required(VERSION 3.18)

project(ModelViewer)

find_package(assimp REQUIRED)
find_package(Freetype REQUIRED)
find_package(Qt5 REQUIRED COMPONENTS Gui OpenGL)

include(GNUInstallDirs)

if(WIN32)
    find_package(Qt5 REQUIRED COMPONENTS WinExtras)
endif()

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)

add_executable(ModelViewer)

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/config.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/config.h"
  @ONLY
)

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

if(WIN32)
    target_link_libraries(ModelViewer
    PRIVATE
        Qt5::WinExtras
)
endif()

set_target_properties(ModelViewer PROPERTIES AUTOMOC TRUE)


install(TARGETS ModelViewer)
install(DIRECTORY fonts shaders
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}"
)
