#pragma once

#include <QString>

namespace abrviz {

/// Version of abr-viz, taken from the CMake project version (e.g. "0.1.0").
[[nodiscard]] QString versionString();

} // namespace abrviz
