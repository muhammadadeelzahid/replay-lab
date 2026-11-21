# The GUI is a library so the tests can create the same widgets the executable uses.
add_library(abrviz_gui STATIC
    format.h
    rung_colors.h
    main_window.h
    main_window.cpp
    session.h
    session.cpp
    time_cursor.h
    time_cursor.cpp
    library_view.h
    library_view.cpp
    summary_panel.h
    summary_panel.cpp
    segment_timeline.h
    segment_timeline.cpp
    chart_series.h
    charts_panel.h
    charts_panel.cpp
    log_console.h
    log_console.cpp
    replay_player.h
    replay_player.cpp
    report_view.h
    report_view.cpp
    icons.h
    icons.cpp
    transport_bar.h
    transport_bar.cpp
    moment_panel.h
    moment_panel.cpp
    branding.h
    segmented_control.h
    segmented_control.cpp
    source_list.h
    source_list.cpp
    settings_dialog.h
    settings_dialog.cpp
    network_path_view.h
    network_path_view.cpp
    thumbnail_cache.h
    thumbnail_cache.cpp
    home_widgets.h
    home_widgets.cpp
    theme.h
    theme.cpp
    algorithm_colors.h
    run_pane.h
    run_pane.cpp
    compare_view.h
    compare_view.cpp
    wipe_view.h
    wipe_view.cpp
    scoreboard.h
    scoreboard.cpp)

qt_add_resources(abrviz_gui "abrviz_resources"
    PREFIX "/abrviz"
    BASE resources
    FILES resources/app-icon.svg)

target_sources(abrviz_gui PRIVATE playback_activity.h background_mode.h)
if(APPLE)
    enable_language(OBJCXX)
    target_sources(abrviz_gui PRIVATE playback_activity_mac.mm background_mode_mac.mm)
    set_source_files_properties(playback_activity_mac.mm background_mode_mac.mm
        PROPERTIES COMPILE_OPTIONS "-fno-objc-arc")
    target_link_libraries(abrviz_gui PRIVATE "-framework Foundation" "-framework AppKit")
else()
    target_sources(abrviz_gui PRIVATE playback_activity.cpp background_mode.cpp)
endif()

target_include_directories(abrviz_gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(abrviz_gui PUBLIC
    abrviz_core
    abrviz_media
    Qt6::Widgets
    Qt6::Charts
    Qt6::MultimediaWidgets
    Qt6::Svg)
abrviz_set_warnings(abrviz_gui)

qt_add_executable(abrviz main.cpp)
target_link_libraries(abrviz PRIVATE abrviz_gui)

# The application is called SatReplay: "SatReplay.app" on macOS (with its icon),
# satreplay elsewhere.
if(APPLE)
    set(ABRVIZ_ICNS ${CMAKE_CURRENT_SOURCE_DIR}/resources/AppIcon.icns)
    target_sources(abrviz PRIVATE ${ABRVIZ_ICNS})
    set_source_files_properties(${ABRVIZ_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(abrviz PROPERTIES
        OUTPUT_NAME "SatReplay"
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "SatReplay"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.muhammadadeelzahid.satreplay"
        MACOSX_BUNDLE_ICON_FILE AppIcon.icns
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_COPYRIGHT "MIT licence")
    # Touch the bundle after each build so Finder and the Dock pick up a changed icon instead of
    # the one they cached for this path.
    add_custom_command(TARGET abrviz POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E touch "$<TARGET_BUNDLE_DIR:abrviz>"
        VERBATIM)
else()
    set_target_properties(abrviz PROPERTIES OUTPUT_NAME "satreplay")
endif()

if(WIN32)
    # A GUI program (no console window) with the application icon.
    set_target_properties(abrviz PROPERTIES WIN32_EXECUTABLE TRUE)
    target_sources(abrviz PRIVATE ${PROJECT_SOURCE_DIR}/packaging/windows/app.rc)
    target_link_libraries(abrviz PRIVATE psapi)
endif()

# Installation, used by the release packages (the Linux AppImage is built from the install tree).
include(GNUInstallDirs)
install(TARGETS abrviz
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(UNIX AND NOT APPLE)
    install(FILES ${PROJECT_SOURCE_DIR}/packaging/linux/satreplay.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES ${PROJECT_SOURCE_DIR}/packaging/linux/satreplay.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
endif()
