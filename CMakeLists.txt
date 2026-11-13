cmake_minimum_required(VERSION 3.24)

project(abrviz
    VERSION 0.1.0
    DESCRIPTION "Viewer for ns-3 adaptive video streaming simulations"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ABR_WITH_GSTREAMER "Build the GStreamer replay engine" ON)
option(ABR_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core Concurrent Widgets Charts Multimedia MultimediaWidgets Test)
qt_standard_project_setup()

if(ABR_WITH_GSTREAMER)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GST IMPORTED_TARGET
        gstreamer-1.0>=1.22 gstreamer-app-1.0 gstreamer-video-1.0)
    if(NOT GST_FOUND)
        message(FATAL_ERROR
            "GStreamer development files were not found by pkg-config.\n"
            "Install them (macOS: brew install gstreamer) or configure with "
            "-DABR_WITH_GSTREAMER=OFF to build only the pre-rendered engine.")
    endif()

    # Homebrew's libffi.pc can point at a macOS SDK that is not installed (when the OS is newer
    # than the Command Line Tools). Our code never includes ffi.h, so drop include directories
    # that do not exist instead of failing the configure step.
    get_target_property(_gst_includes PkgConfig::GST INTERFACE_INCLUDE_DIRECTORIES)
    set(_gst_existing_includes "")
    foreach(_dir IN LISTS _gst_includes)
        if(EXISTS "${_dir}")
            list(APPEND _gst_existing_includes "${_dir}")
        else()
            message(STATUS "Ignoring missing GStreamer include directory: ${_dir}")
        endif()
    endforeach()
    set_target_properties(PkgConfig::GST PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${_gst_existing_includes}")
endif()

include(cmake/Warnings.cmake)

enable_testing()

add_subdirectory(core)
add_subdirectory(media)
add_subdirectory(app)
add_subdirectory(tests)
