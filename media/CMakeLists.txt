add_library(abrviz_media STATIC
    include/abrviz/media/engine_support.h
    src/engine_support.cpp)

target_include_directories(abrviz_media PUBLIC include)
target_link_libraries(abrviz_media PUBLIC abrviz_core Qt6::Multimedia)

if(ABR_WITH_GSTREAMER)
    target_link_libraries(abrviz_media PRIVATE PkgConfig::GST)
    target_compile_definitions(abrviz_media PUBLIC ABRVIZ_WITH_GSTREAMER=1)
else()
    target_compile_definitions(abrviz_media PUBLIC ABRVIZ_WITH_GSTREAMER=0)
endif()

abrviz_set_warnings(abrviz_media)
