cmake_minimum_required(VERSION 3.24)

project(abrviz
    VERSION 2.0.0
    DESCRIPTION "Viewer for ns-3 adaptive video streaming simulations"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ABR_WITH_GSTREAMER "Build the GStreamer replay engine" ON)
option(ABR_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(ABR_BUILD_TESTS "Build the tests" ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core Concurrent Widgets Charts Multimedia MultimediaWidgets Svg Test)
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

# API documentation: `cmake --build --preset <preset> --target docs` writes HTML to
# build/<preset>/docs/html (needs Doxygen; Graphviz adds class and call graphs).
find_package(Doxygen OPTIONAL_COMPONENTS dot)
if(DOXYGEN_FOUND)
    set(DOXYGEN_PROJECT_NAME "ReplayLab")
    set(DOXYGEN_PROJECT_NUMBER ${PROJECT_VERSION})
    set(DOXYGEN_PROJECT_BRIEF "Replays ns-3 adaptive streaming runs over satellite 5G with IAB")
    set(DOXYGEN_PROJECT_LOGO ${CMAKE_SOURCE_DIR}/app/resources/app-icon.svg)
    set(DOXYGEN_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/docs)
    set(DOXYGEN_USE_MDFILE_AS_MAINPAGE ${CMAKE_SOURCE_DIR}/README.md)
    set(DOXYGEN_IMAGE_PATH ${CMAKE_SOURCE_DIR})
    set(DOXYGEN_FILE_PATTERNS *.h *.cpp *.mm *.md)
    set(DOXYGEN_EXTENSION_MAPPING mm=C++)
    set(DOXYGEN_EXCLUDE_PATTERNS */build/* */viewer_bundles/* */tests/data/*)
    set(DOXYGEN_EXTRACT_PRIVATE YES)
    set(DOXYGEN_EXTRACT_STATIC YES)
    set(DOXYGEN_EXTRACT_ANON_NSPACES YES)
    set(DOXYGEN_EXTRACT_LOCAL_CLASSES YES)
    set(DOXYGEN_BUILTIN_STL_SUPPORT YES)
    set(DOXYGEN_SOURCE_BROWSER YES)
    set(DOXYGEN_GENERATE_TREEVIEW YES)
    set(DOXYGEN_GENERATE_LATEX NO)
    set(DOXYGEN_QUIET YES)
    set(DOXYGEN_WARN_IF_UNDOCUMENTED YES)
    set(DOXYGEN_WARN_NO_PARAMDOC YES)
    set(DOXYGEN_WARN_LOGFILE ${CMAKE_BINARY_DIR}/docs/warnings.txt)
    # Qt's moc keywords, so signals and slots are listed as functions.
    set(DOXYGEN_MACRO_EXPANSION YES)
    set(DOXYGEN_EXPAND_ONLY_PREDEF YES)
    set(DOXYGEN_PREDEFINED
        "Q_OBJECT=" "Q_GADGET=" "Q_ENUM(x)=" "Q_PROPERTY(x)=" "Q_DECLARE_METATYPE(x)="
        "signals=public" "Q_SIGNALS=public" "slots=" "Q_SLOTS=" "ABRVIZ_WITH_GSTREAMER=1")
    if(DOXYGEN_DOT_FOUND)
        set(DOXYGEN_HAVE_DOT YES)
        set(DOXYGEN_CALL_GRAPH NO)
        set(DOXYGEN_COLLABORATION_GRAPH YES)
    else()
        set(DOXYGEN_HAVE_DOT NO)
    endif()
    doxygen_add_docs(docs
        README.md docs core/include core/src media app tools
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating the API documentation with Doxygen")
endif()

add_subdirectory(core)
add_subdirectory(media)
add_subdirectory(app)
add_subdirectory(tools)
if(ABR_BUILD_TESTS)
    add_subdirectory(tests)
endif()
