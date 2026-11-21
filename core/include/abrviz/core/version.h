#pragma once

/**
 * @file
 * @brief Version string of ReplayLab.
 */

#include <QString>

namespace abrviz {

/**
 * @brief Returns the version of ReplayLab.
 *
 * Taken from the CMake project version.
 *
 * @return The version, for example "0.1.0".
 */
[[nodiscard]] QString versionString();

} // namespace abrviz
