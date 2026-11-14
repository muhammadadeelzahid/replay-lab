#pragma once

#include <QString>

namespace abrviz {

/// The name the application shows (menu bar, window titles, About, Finder). The project and its
/// libraries keep the name abr-viz.
inline QString appDisplayName()
{
    return QStringLiteral("Stream Inspector");
}

} // namespace abrviz
