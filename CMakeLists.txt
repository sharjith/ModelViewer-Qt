
cmake_minimum_required(VERSION 3.18)

project(ModelViewer)

find_package(assimp REQUIRED)
find_package(Freetype REQUIRED)

find_package(Qt6 COMPONENTS Core Gui OpenGLWidgets Widgets)
if(Qt6_FOUND)
    set(QT_VERSION_MAJOR 6)
else()
    find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui OpenGL Widgets)

    if(WIN32)
        find_package(Qt5 REQUIRED COMPONENTS WinExtras)
    endif()

    set(QT_VERSION_MAJOR 5)
endif()

message("Using Qt${QT_VERSION_MAJOR}")

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
        Qt::Core
        Qt::Gui
        $<$<EQUAL:${QT_VERSION_MAJOR},6>:Qt::OpenGLWidgets>
        $<$<EQUAL:${QT_VERSION_MAJOR},5>:Qt::OpenGL>
        Qt::Widgets
)

if(WIN32)
    target_sources(ModelViewer
        PRIVATE
            ModelViewer.rc
    )

    if(QT_VERSION_MAJOR EQUAL 5)
        target_link_libraries(ModelViewer
            PRIVATE
                Qt::WinExtras
        )
    endif()

endif()

set_target_properties(ModelViewer PROPERTIES AUTOMOC TRUE)

install(TARGETS ModelViewer)
install(DIRECTORY fonts shaders textures
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}"
)

if(WIN32)
   install(TARGETS ModelViewer
        RUNTIME_DEPENDENCIES
            PRE_EXCLUDE_REGEXES
		        [[hvsifiletrust.*]]
		        [[pdmutilities.*]]
		        [[api-ms-win-.*]]
		        [[ext-ms-.*]]
                [[qt6.*\\.dll]]                                                         # qt_generate_deploy_app_script will take care about Qt's libs
            POST_EXCLUDE_REGEXES
                [[.*system32.*.dll]]
    )

    if(QT_VERSION_MAJOR EQUAL 6)
        qt_generate_deploy_app_script(
            TARGET ModelViewer
            OUTPUT_SCRIPT deploy_script
            NO_UNSUPPORTED_PLATFORM_ERROR
            NO_COMPILER_RUNTIME
        )

        install(SCRIPT ${deploy_script})
    endif()
endif()
