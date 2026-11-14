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
 * The project and its libraries keep the name abr-viz.
 *
 * @return The display name, "Stream Inspector".
 */
inline QString appDisplayName()
{
    return QStringLiteral("Stream Inspector");
}

} // namespace abrviz
