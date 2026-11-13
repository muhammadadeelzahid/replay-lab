# abrviz_add_test(<name> SOURCES <files...> LIBS <targets...>)
# Builds one Qt Test executable and registers it with CTest. GUI tests run offscreen.
function(abrviz_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
    qt_add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARG_LIBS})
    abrviz_set_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

abrviz_add_test(tst_core_version
    SOURCES tst_core_version.cpp
    LIBS abrviz_core)

abrviz_add_test(tst_media_engine_support
    SOURCES tst_media_engine_support.cpp
    LIBS abrviz_media)

abrviz_add_test(tst_main_window
    SOURCES tst_main_window.cpp
    LIBS abrviz_gui)
