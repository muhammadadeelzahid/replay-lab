# The GUI is a library so the tests can create the same widgets the executable uses.
add_library(abrviz_gui STATIC
    main_window.h
    main_window.cpp)

target_include_directories(abrviz_gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(abrviz_gui PUBLIC
    abrviz_core
    abrviz_media
    Qt6::Widgets
    Qt6::Charts
    Qt6::MultimediaWidgets)
abrviz_set_warnings(abrviz_gui)

qt_add_executable(abrviz main.cpp)
target_link_libraries(abrviz PRIVATE abrviz_gui)
abrviz_set_warnings(abrviz)
