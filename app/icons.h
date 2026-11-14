#pragma once

/**
 * @file
 * @brief The application's line icons, its accent colour and its application icon.
 */

#include <QColor>
#include <QIcon>
#include <QPalette>

namespace abrviz {

/**
 * @brief The line icons used in the toolbar, the sidebar and the transport bar.
 */
enum class Glyph {
    Library,
    Replay,
    Analysis,
    Logs,
    Report,
    OpenFolder,
    OpenBundle,
    Play,
    Pause,
    StepBack,
    StepForward,
    PreviousSegment,
    NextSegment,
    Render,
    Engine,
    Overlay,
    Film,
    Inspector,
    Sidebar,
    Settings,
    Search,
    Satellite,
    Relay,
    Server,
    Device,
};

/**
 * @brief A line icon drawn in one colour, sharp at every size Qt asks for.
 *
 * The icon is rendered from SVG at the requested size and device pixel ratio. Disabled icons
 * are drawn at 35% opacity.
 *
 * @param glyph The icon to draw.
 * @param color The colour of the strokes and fills; normally the palette's text colour, so the
 *              icon suits light and dark themes. Its alpha is honoured.
 * @param selected The colour used for selected items; if invalid, `color` is used for them too.
 * @return The icon.
 */
[[nodiscard]] QIcon glyphIcon(Glyph glyph, const QColor& color, const QColor& selected = QColor());

/**
 * @brief The accent colour for icons and highlights: Apple's system blue for the appearance.
 *
 * @note Qt's Highlight on macOS is the darker selection colour, so it is not used here.
 *
 * @param palette The palette whose window colour decides between the light and dark blue.
 * @return #0A84FF for a dark window colour, #007AFF for a light one.
 */
[[nodiscard]] QColor accentColor(const QPalette& palette);

/**
 * @brief The application icon: a play symbol beamed down by a satellite, under a magnifying
 *        glass.
 *
 * @return The icon, loaded from the `:/abrviz/app-icon.svg` resource.
 */
[[nodiscard]] QIcon appIcon();

} // namespace abrviz
