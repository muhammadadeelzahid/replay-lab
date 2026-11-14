add_library(abrviz_media STATIC
    include/abrviz/media/engine_support.h
    include/abrviz/media/replay_source.h
    include/abrviz/media/prerendered_source.h
    include/abrviz/media/replay_source_factory.h
    src/engine_support.cpp
    src/prerendered_source.cpp
    src/replay_source_factory.cpp)

target_include_directories(abrviz_media PUBLIC include)
target_link_libraries(abrviz_media PUBLIC abrviz_core Qt6::Multimedia)

if(ABR_WITH_GSTREAMER)
    # GStreamer headers stay private to this library (media/src/gst).
    target_sources(abrviz_media PRIVATE
        src/gst/gst_ptr.h
        src/gst/pipeline_guard.h
        src/gst/video_frame_map_guard.h
        src/gst/gst_runtime.h
        src/gst/gst_runtime.cpp)
    target_link_libraries(abrviz_media PRIVATE PkgConfig::GST)
    target_compile_definitions(abrviz_media PUBLIC ABRVIZ_WITH_GSTREAMER=1)
else()
    target_compile_definitions(abrviz_media PUBLIC ABRVIZ_WITH_GSTREAMER=0)
endif()

abrviz_set_warnings(abrviz_media)
