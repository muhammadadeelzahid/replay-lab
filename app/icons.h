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
    Laptop,
    Download,
    Chevron,
    Expand,
    Collapse,
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
 * @brief A node of the network path as an app-style tile: its white line symbol on a rounded
 *        square in the node's colour (graphite server, blue satellite, green IAB relay, indigo
 *        laptop).
 *
 * @param glyph Glyph::Server, Glyph::Satellite, Glyph::Relay or Glyph::Laptop (Glyph::Device
 *              gives the laptop too); other glyphs fall back to glyphIcon() in white.
 * @return The icon; it renders sharply at any size.
 */
[[nodiscard]] QIcon nodeIcon(Glyph glyph);

/**
 * @brief A glyph as an app-style tile in a colour: the white symbol on a rounded square shaded
 *        from a lighter to a darker shade of `color`.
 *
 * @param glyph The symbol.
 * @param color The tile's colour, e.g. accentColor().
 * @return The icon; it renders sharply at any size.
 */
[[nodiscard]] QIcon tileIcon(Glyph glyph, const QColor& color);

/**
 * @brief The accent colour for icons and highlights: the coral of the app icon, lighter in dark
 *        mode.
 *
 * @note Qt's Highlight on macOS is the darker selection colour, so it is not used here.
 *
 * @param palette The palette whose window colour decides between the light and dark shade.
 * @return #FF6B4A for a dark window colour, #E9532F for a light one.
 */
[[nodiscard]] QColor accentColor(const QPalette& palette);

/**
 * @brief The application icon: a play symbol circled by the orbit of a satellite.
 *
 * @return The icon, loaded from the `:/abrviz/app-icon.svg` resource.
 */
[[nodiscard]] QIcon appIcon();

} // namespace abrviz
