add_library(abrviz_core STATIC
    include/abrviz/core/version.h
    include/abrviz/core/model/run_data.h
    include/abrviz/core/io/parsed.h
    include/abrviz/core/io/mapped_file.h
    include/abrviz/core/io/text_lines.h
    include/abrviz/core/io/log_tokens.h
    include/abrviz/core/io/bundle_files.h
    include/abrviz/core/io/sim_log_parser.h
    include/abrviz/core/io/policy_log_parser.h
    include/abrviz/core/io/mp4_duration.h
    include/abrviz/core/io/topology_reader.h
    include/abrviz/core/load/load_result.h
    include/abrviz/core/load/segment_shift.h
    include/abrviz/core/load/bundle_reader.h
    include/abrviz/core/load/bundle_loader.h
    include/abrviz/core/util/future_guard.h
    include/abrviz/core/util/parallel_map.h
    include/abrviz/core/load/bundle_library.h
    include/abrviz/core/playback/playback_clock.h
    include/abrviz/core/playback/comparison_clock.h
    include/abrviz/core/metrics/qoe_metrics.h
    include/abrviz/core/logs/log_index.h
    include/abrviz/core/logs/log_filter.h
    include/abrviz/core/util/process_guard.h
    include/abrviz/core/render/render_plan.h
    include/abrviz/core/render/render_cache.h
    include/abrviz/core/render/segment_transcoder.h
    include/abrviz/core/render/render_queue.h
    src/version.cpp
    src/model/run_data.cpp
    src/io/mapped_file.cpp
    src/io/text_lines.cpp
    src/io/bundle_files.cpp
    src/io/sim_log_parser.cpp
    src/io/policy_log_parser.cpp
    src/io/mp4_duration.cpp
    src/io/topology_reader.cpp
    src/load/segment_shift.cpp
    src/load/bundle_reader.cpp
    src/load/bundle_loader.cpp
    src/load/bundle_library.cpp
    src/playback/playback_clock.cpp
    src/playback/comparison_clock.cpp
    src/metrics/qoe_metrics.cpp
    src/metrics/highlights.cpp
    src/logs/log_index.cpp
    src/logs/log_filter.cpp
    src/util/process_guard.cpp
    src/render/render_plan.cpp
    src/render/render_cache.cpp
    src/render/segment_transcoder.cpp
    src/render/render_queue.cpp)

target_include_directories(abrviz_core PUBLIC include)
target_link_libraries(abrviz_core PUBLIC Qt6::Core Qt6::Concurrent)
target_compile_definitions(abrviz_core PRIVATE ABRVIZ_VERSION="${PROJECT_VERSION}")
abrviz_set_warnings(abrviz_core)
