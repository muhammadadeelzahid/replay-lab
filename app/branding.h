#pragma once

/**
 * @file
 * @brief The name the application shows to the user.
 */

#include <QString>

namespace abrviz {

/**
 * @brief The name the application shows (menu bar, window titles, About, Finder).
 *
 * The C++ namespace, the libraries and the build targets keep the name abrviz.
 *
 * @return The display name, "ReplayLab".
 */
inline QString appDisplayName()
{
    return QStringLiteral("ReplayLab");
}

} // namespace abrviz
