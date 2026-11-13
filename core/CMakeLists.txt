add_library(abrviz_core STATIC
    include/abrviz/core/version.h
    src/version.cpp)

target_include_directories(abrviz_core PUBLIC include)
target_link_libraries(abrviz_core PUBLIC Qt6::Core Qt6::Concurrent)
target_compile_definitions(abrviz_core PRIVATE ABRVIZ_VERSION="${PROJECT_VERSION}")
abrviz_set_warnings(abrviz_core)
