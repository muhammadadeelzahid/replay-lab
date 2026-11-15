# abrviz_add_test(<name> SOURCES <files...> LIBS <targets...>)
# Builds one Qt Test executable and registers it with CTest. GUI tests run offscreen.
function(abrviz_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
    qt_add_executable(${name} ${ARG_SOURCES} test_paths.h synthetic_run.h)
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARG_LIBS})
    target_compile_definitions(${name} PRIVATE
        ABRVIZ_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
        ABRVIZ_BUNDLES_DIR="${PROJECT_SOURCE_DIR}/viewer_bundles")
    abrviz_set_warnings(${name})
    # Qt's QTRY_* macros narrow a std::chrono count to int inside their expansion, which GCC
    # reports in our files with -Wconversion; only the tests use them.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${name} PRIVATE -Wno-conversion)
    endif()
    add_test(NAME ${name} COMMAND ${name})
    # Rendered videos go to the build folder, never to the user's cache.
    set_tests_properties(${name} PROPERTIES TIMEOUT 300 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;ABRVIZ_RENDER_CACHE=${CMAKE_CURRENT_BINARY_DIR}/render-cache")
endfunction()

abrviz_add_test(tst_core_version
    SOURCES tst_core_version.cpp
    LIBS abrviz_core)

abrviz_add_test(tst_core_io
    SOURCES tst_core_io.cpp
    LIBS abrviz_core)

abrviz_add_test(tst_bundle_reader
    SOURCES tst_bundle_reader.cpp
    LIBS abrviz_core)

abrviz_add_test(tst_bundle_loader
    SOURCES tst_bundle_loader.cpp
    LIBS abrviz_core)

abrviz_add_test(tst_bundle_library
    SOURCES tst_bundle_library.cpp
    LIBS abrviz_core)

abrviz_add_test(tst_log_index
    SOURCES tst_log_index.cpp
    LIBS abrviz_core)

abrviz_add_test(tst_render
    SOURCES tst_render.cpp
    LIBS abrviz_core)

abrviz_add_test(tst_playback_metrics
    SOURCES tst_playback_metrics.cpp
    LIBS abrviz_core)

abrviz_add_test(tst_media_engine_support
    SOURCES tst_media_engine_support.cpp
    LIBS abrviz_media)

abrviz_add_test(tst_main_window
    SOURCES tst_main_window.cpp
    LIBS abrviz_gui)

abrviz_add_test(tst_charts_panel
    SOURCES tst_charts_panel.cpp
    LIBS abrviz_gui)

abrviz_add_test(tst_log_console
    SOURCES tst_log_console.cpp
    LIBS abrviz_gui)

if(ABR_WITH_GSTREAMER)
    abrviz_add_test(tst_gst_wrappers
        SOURCES tst_gst_wrappers.cpp
        LIBS abrviz_media PkgConfig::GST)
    target_include_directories(tst_gst_wrappers PRIVATE ${PROJECT_SOURCE_DIR}/media/src)

    abrviz_add_test(tst_gst_segment_source
        SOURCES tst_gst_segment_source.cpp
        LIBS abrviz_media PkgConfig::GST)
    target_include_directories(tst_gst_segment_source PRIVATE ${PROJECT_SOURCE_DIR}/media/src)
endif()

abrviz_add_test(tst_core_edge_cases
    SOURCES tst_core_edge_cases.cpp
    LIBS abrviz_core)

abrviz_add_test(tst_playback_edge_cases
    SOURCES tst_playback_edge_cases.cpp
    LIBS abrviz_core)

abrviz_add_test(tst_widgets
    SOURCES tst_widgets.cpp
    LIBS abrviz_gui)

abrviz_add_test(tst_integration
    SOURCES tst_integration.cpp
    LIBS abrviz_gui)

# Engine edge cases (both engines; the GStreamer part is compiled only with GStreamer).
abrviz_add_test(tst_engine_edge_cases
    SOURCES tst_engine_edge_cases.cpp
    LIBS abrviz_media abrviz_gui)
target_include_directories(tst_engine_edge_cases PRIVATE ${PROJECT_SOURCE_DIR}/media/src)
if(ABR_WITH_GSTREAMER)
    target_link_libraries(tst_engine_edge_cases PRIVATE PkgConfig::GST)
endif()
if(APPLE)
    # Qt's AVFoundation backend delivers media through the Cocoa run loop, which the offscreen
    # platform does not run: a second pass with the Cocoa platform tests the pre-rendered
    # engine. Each pass skips the other's cases.
    add_test(NAME tst_engine_edge_cases_cocoa COMMAND tst_engine_edge_cases)
    set_tests_properties(tst_engine_edge_cases_cocoa PROPERTIES TIMEOUT 300 ENVIRONMENT
        "QT_QPA_PLATFORM=cocoa;ABRVIZ_RENDER_CACHE=${CMAKE_CURRENT_BINARY_DIR}/render-cache")
    if(CMAKE_CXX_FLAGS MATCHES "sanitize=thread")
        # The TSan runtime can deadlock when CoreMedia threads exit (see tst_replay_source).
        set_tests_properties(tst_engine_edge_cases_cocoa PROPERTIES DISABLED TRUE)
    endif()
endif()

abrviz_add_test(tst_replay_source
    SOURCES tst_replay_source.cpp
    LIBS abrviz_media abrviz_gui)
if(APPLE)
    # Qt's AVFoundation backend delivers media through the Cocoa run loop, which the offscreen
    # platform does not run.
    set_tests_properties(tst_replay_source PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=cocoa;ABRVIZ_RENDER_CACHE=${CMAKE_CURRENT_BINARY_DIR}/render-cache")
endif()

abrviz_add_test(tst_replay_player
    SOURCES tst_replay_player.cpp
    LIBS abrviz_gui)
if(APPLE)
    set_tests_properties(tst_replay_player PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=cocoa;ABRVIZ_RENDER_CACHE=${CMAKE_CURRENT_BINARY_DIR}/render-cache")
endif()

if(APPLE AND CMAKE_CXX_FLAGS MATCHES "sanitize=thread")
    # Under ThreadSanitizer on macOS the TSan runtime can deadlock inside itself when CoreMedia
    # threads exit (pthread_exit -> free -> SlotLock while AppKit holds the ObjC weak table
    # lock). The tests pass under the debug and ASan builds; the GStreamer engine tests cover
    # engine threading under TSan.
    set_tests_properties(tst_replay_source tst_replay_player PROPERTIES DISABLED TRUE)
endif()

# Command-line tests of the application binary. tst_integration starts it with QProcess
# (always offscreen, with its settings in a temporary folder); the cli_app entry runs just
# those test functions, and the tst_integration entry skips them so they run once.
add_dependencies(tst_integration abrviz)
target_compile_definitions(tst_integration PRIVATE ABRVIZ_APP_PATH="$<TARGET_FILE:abrviz>")
set_tests_properties(tst_integration PROPERTIES
    ENVIRONMENT_MODIFICATION "ABRVIZ_INTEGRATION_SKIP_CLI=set:1")
add_test(NAME cli_app COMMAND tst_integration
    cliHelpAndVersion cliRejectsUnknownPageAndEngine cliProfileAndRecordNeedABundle
    cliScreenshotWritesAPng cliRecordWritesNumberedFrames cliNonexistentPathDoesNotCrash)
set_tests_properties(cli_app PROPERTIES TIMEOUT 600 ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen;ABRVIZ_RENDER_CACHE=${CMAKE_CURRENT_BINARY_DIR}/render-cache")
