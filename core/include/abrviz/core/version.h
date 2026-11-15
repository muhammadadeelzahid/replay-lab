#pragma once

/**
 * @file
 * @brief Version string of SatReplay.
 */

#include <QString>

namespace abrviz {

/**
 * @brief Returns the version of SatReplay.
 *
 * Taken from the CMake project version.
 *
 * @return The version, for example "0.1.0".
 */
[[nodiscard]] QString versionString();

} // namespace abrviz
