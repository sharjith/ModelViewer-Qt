
cmake_minimum_required(VERSION 3.18)

project(ModelViewer)

find_package(assimp REQUIRED)
find_package(Freetype REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL Widgets)

include(GNUInstallDirs)

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

qt_wrap_ui(ui_wrapped ${ui_sources})
qt_add_resources(resources ModelViewer.qrc)

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
        Qt::OpenGL
        Qt::Widgets
)

if(WIN32)
    target_sources(ModelViewer
        PRIVATE
            ModelViewer.rc
    )
endif()

set_target_properties(ModelViewer PROPERTIES AUTOMOC TRUE)


install(TARGETS ModelViewer)
install(DIRECTORY fonts shaders textures
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}"
)
