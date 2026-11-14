#pragma once

/**
 * @file
 * @brief Version string of abr-viz.
 */

#include <QString>

namespace abrviz {

/**
 * @brief Returns the version of abr-viz.
 *
 * Taken from the CMake project version.
 *
 * @return The version, for example "0.1.0".
 */
[[nodiscard]] QString versionString();

} // namespace abrviz
